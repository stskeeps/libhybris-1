/*
 * Copyright (C) 2013 libhybris
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hwcomposer_window.h"

#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>

#include <android/version.h>
extern "C" {
#include <android/sync/sync.h>
};



#include "logging.h"

static pthread_cond_t _cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;


HWComposerNativeWindowBuffer::HWComposerNativeWindowBuffer(unsigned int width,
                            unsigned int height,
                            unsigned int format,
                            unsigned int usage,
			    int fenceFd)
{
    ANativeWindowBuffer::width  = width;
    ANativeWindowBuffer::height = height;
    ANativeWindowBuffer::format = format;
    ANativeWindowBuffer::usage  = usage;
    this->fenceFd = fenceFd;
    this->rendered = 0;

    TRACE("width=%d height=%d format=x%x usage=x%x this=%p",
        width, height, format, usage, fenceFd, this);
}



HWComposerNativeWindowBuffer::~HWComposerNativeWindowBuffer()
{
    TRACE("%p", this);
}


////////////////////////////////////////////////////////////////////////////////
HWComposerNativeWindow::HWComposerNativeWindow(unsigned int width, unsigned int height, unsigned int format)
{
    m_alloc = NULL;
    m_width = width;
    m_height = height;
    m_bufFormat = format;
    m_usage = GRALLOC_USAGE_HW_COMPOSER;
}

void HWComposerNativeWindow::setup(gralloc_module_t* gralloc, alloc_device_t* alloc)
{
    m_alloc = alloc;
    setBufferCount(2);
}



HWComposerNativeWindow::~HWComposerNativeWindow()
{
    destroyBuffers();
}



void HWComposerNativeWindow::destroyBuffers()
{
}




/*
 * Set the swap interval for this surface.
 *
 * Returns 0 on success or -errno on error.
 */
int HWComposerNativeWindow::setSwapInterval(int interval)
{
    TRACE("interval=%i", interval);
    return 0;
}


/*
 * Hook called by EGL to acquire a buffer. This call may block if no
 * buffers are available.
 *
 * The window holds a reference to the buffer between dequeueBuffer and
 * either queueBuffer or cancelBuffer, so clients only need their own
 * reference if they might use the buffer after queueing or canceling it.
 * Holding a reference to a buffer after queueing or canceling it is only
 * allowed if a specific buffer count has been set.
 *
 * The libsync fence file descriptor returned in the int pointed to by the
 * fenceFd argument will refer to the fence that must signal before the
 * dequeued buffer may be written to.  A value of -1 indicates that the
 * caller may access the buffer immediately without waiting on a fence.  If
 * a valid file descriptor is returned (i.e. any value except -1) then the
 * caller is responsible for closing the file descriptor.
 *
 * Returns 0 on success or -errno on error.
 */
int HWComposerNativeWindow::dequeueBuffer(BaseNativeWindowBuffer** buffer, int *fenceFd)
{
    TRACE("%u", pthread_self());

    pthread_mutex_lock(&_mutex);

    *buffer = this->m_backBuf;
    *fenceFd = this->m_backBuf->fenceFd;

    TRACE("%u DONE --> %p %i", pthread_self(), buffer, *fenceFd);
    pthread_mutex_unlock(&_mutex);

    return 0;
}

/*
 * Hook called by EGL when modifications to the render buffer are done.
 * This unlocks and post the buffer.
 *
 * The window holds a reference to the buffer between dequeueBuffer and
 * either queueBuffer or cancelBuffer, so clients only need their own
 * reference if they might use the buffer after queueing or canceling it.
 * Holding a reference to a buffer after queueing or canceling it is only
 * allowed if a specific buffer count has been set.
 *
 * The fenceFd argument specifies a libsync fence file descriptor for a
 * fence that must signal before the buffer can be accessed.  If the buffer
 * can be accessed immediately then a value of -1 should be used.  The
 * caller must not use the file descriptor after it is passed to
 * queueBuffer, and the ANativeWindow implementation is responsible for
 * closing it.
 *
 * Returns 0 on success or -errno on error.
 */
int HWComposerNativeWindow::queueBuffer(BaseNativeWindowBuffer* buffer, int fenceFd)
{
    TRACE("%u %d", pthread_self(), fenceFd);
    HWComposerNativeWindowBuffer *tmp;

    pthread_mutex_lock(&_mutex);
    
    assert(buffer == static_cast<HWComposerNativeWindowBuffer *>(this->m_backBuf)); 
    /* XXX latency hotspot */ 
    sync_wait(fenceFd, -1);
    close(fenceFd);    
    
    this->m_backBuf->fenceFd = -1;
    this->m_backBuf->rendered = 1;

    tmp = this->m_frontBuf;
    this->m_frontBuf = this->m_backBuf;
    this->m_backBuf = tmp;
    this->m_backBuf->rendered = 0;

    pthread_cond_signal(&_cond);
    pthread_mutex_unlock(&_mutex);

    return 0;
}


/*
 * Hook used to cancel a buffer that has been dequeued.
 * No synchronization is performed between dequeue() and cancel(), so
 * either external synchronization is needed, or these functions must be
 * called from the same thread.
 *
 * The window holds a reference to the buffer between dequeueBuffer and
 * either queueBuffer or cancelBuffer, so clients only need their own
 * reference if they might use the buffer after queueing or canceling it.
 * Holding a reference to a buffer after queueing or canceling it is only
 * allowed if a specific buffer count has been set.
 *
 * The fenceFd argument specifies a libsync fence file decsriptor for a
 * fence that must signal before the buffer can be accessed.  If the buffer
 * can be accessed immediately then a value of -1 should be used.
 *
 * Note that if the client has not waited on the fence that was returned
 * from dequeueBuffer, that same fence should be passed to cancelBuffer to
 * ensure that future uses of the buffer are preceded by a wait on that
 * fence.  The caller must not use the file descriptor after it is passed
 * to cancelBuffer, and the ANativeWindow implementation is responsible for
 * closing it.
 *
 * Returns 0 on success or -errno on error.
 */
int HWComposerNativeWindow::cancelBuffer(BaseNativeWindowBuffer* buffer, int fenceFd)
{
    TRACE("");
}



int HWComposerNativeWindow::lockBuffer(BaseNativeWindowBuffer* buffer)
{
    TRACE("%u", pthread_self());
    return NO_ERROR;
}


/*
 * see NATIVE_WINDOW_FORMAT
 */
unsigned int HWComposerNativeWindow::width() const
{
    unsigned int rv = m_width;
    TRACE("width=%i", rv);
    return rv;
}


/*
 * see NATIVE_WINDOW_HEIGHT
 */
unsigned int HWComposerNativeWindow::height() const
{
    unsigned int rv = m_height;
    TRACE("height=%i", rv);
    return rv;
}


/*
 * see NATIVE_WINDOW_FORMAT
 */
unsigned int HWComposerNativeWindow::format() const
{
    unsigned int rv = m_bufFormat;
    TRACE("format=x%x", rv);
    return rv;
}


/*
 * Default width and height of ANativeWindow buffers, these are the
 * dimensions of the window buffers irrespective of the
 * NATIVE_WINDOW_SET_BUFFERS_DIMENSIONS call and match the native window
 * size unless overridden by NATIVE_WINDOW_SET_BUFFERS_USER_DIMENSIONS.
 */
/*
 * see NATIVE_WINDOW_DEFAULT_HEIGHT
 */
unsigned int HWComposerNativeWindow::defaultHeight() const
{
    unsigned int rv = m_height;
    TRACE("height=%i", rv);
    return rv;
}


/*
 * see BaseNativeWindow::_query(NATIVE_WINDOW_DEFAULT_WIDTH)
 */
unsigned int HWComposerNativeWindow::defaultWidth() const
{
    unsigned int rv = m_width;
    TRACE("width=%i", rv);
    return rv;
}


/*
 * see NATIVE_WINDOW_QUEUES_TO_WINDOW_COMPOSER
 */
unsigned int HWComposerNativeWindow::queueLength() const
{
    TRACE("");
    return 0;
}


/*
 * see NATIVE_WINDOW_CONCRETE_TYPE
 */
unsigned int HWComposerNativeWindow::type() const
{
    TRACE("");
    return NATIVE_WINDOW_FRAMEBUFFER;
}


/*
 * see NATIVE_WINDOW_TRANSFORM_HINT
 */
unsigned int HWComposerNativeWindow::transformHint() const
{
    TRACE("");
    return 0;
}



/*
 *  native_window_set_usage(..., usage)
 *  Sets the intended usage flags for the next buffers
 *  acquired with (*lockBuffer)() and on.
 *  By default (if this function is never called), a usage of
 *      GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE
 *  is assumed.
 *  Calling this function will usually cause following buffers to be
 *  reallocated.
 */
int HWComposerNativeWindow::setUsage(int usage)
{
    int need_realloc = (m_usage != usage);
    TRACE("usage=x%x realloc=%d", usage, need_realloc);
    m_usage = usage;
    if (need_realloc)
        this->setBufferCount(2);

    return NO_ERROR;
}


/*
 * native_window_set_buffers_format(..., int format)
 * All buffers dequeued after this call will have the format specified.
 *
 * If the specified format is 0, the default buffer format will be used.
 */
int HWComposerNativeWindow::setBuffersFormat(int format)
{
    int need_realloc = (format != m_bufFormat);
    TRACE("format=x%x realloc=%d", format, need_realloc);
    m_bufFormat = format;
    if (need_realloc)
        this->setBufferCount(2);
    return NO_ERROR;
}


/*
 * native_window_set_buffer_count(..., count)
 * Sets the number of buffers associated with this native window.
 */
int HWComposerNativeWindow::setBufferCount(int cnt)
{
    TRACE("cnt=%d", cnt);
    int err=NO_ERROR;
    pthread_mutex_lock(&_mutex);
    assert(cnt == 2);

    HWComposerNativeWindowBuffer *fbnb = new HWComposerNativeWindowBuffer(
                            m_width, m_height, m_bufFormat,
                            m_usage|GRALLOC_USAGE_HW_FB, -1);

    fbnb->common.incRef(&fbnb->common);

    err = m_alloc->alloc(m_alloc,
                            m_width, m_height, m_bufFormat,
                            m_usage|GRALLOC_USAGE_HW_COMPOSER|GRALLOC_USAGE_HW_FB,
                            &fbnb->handle, &fbnb->stride);

    TRACE("buffer front is at %p (native %p) err=%s handle=%i stride=%i",
               fbnb, (ANativeWindowBuffer*)fbnb,
                strerror(-err), fbnb->handle, fbnb->stride);

    assert(err == 0);

    this->m_frontBuf = fbnb;

    fbnb = new HWComposerNativeWindowBuffer(
                            m_width, m_height, m_bufFormat,
                            m_usage|GRALLOC_USAGE_HW_FB, -1);

    fbnb->common.incRef(&fbnb->common);

    err = m_alloc->alloc(m_alloc,
                            m_width, m_height, m_bufFormat,
                            m_usage|GRALLOC_USAGE_HW_COMPOSER|GRALLOC_USAGE_HW_FB,
                            &fbnb->handle, &fbnb->stride);

    TRACE("buffer back is at %p (native %p) err=%s handle=%i stride=%i",
                fbnb, (ANativeWindowBuffer*)fbnb,
                strerror(-err), fbnb->handle, fbnb->stride);

    assert(err == 0);
    this->m_backBuf = fbnb;

    pthread_mutex_unlock(&_mutex);

    return err;
}

/*
 * native_window_set_buffers_dimensions(..., int w, int h)
 * All buffers dequeued after this call will have the dimensions specified.
 * In particular, all buffers will have a fixed-size, independent from the
 * native-window size. They will be scaled according to the scaling mode
 * (see native_window_set_scaling_mode) upon window composition.
 *
 * If w and h are 0, the normal behavior is restored. That is, dequeued buffers
 * following this call will be sized to match the window's size.
 *
 * Calling this function will reset the window crop to a NULL value, which
 * disables cropping of the buffers.
 */
int HWComposerNativeWindow::setBuffersDimensions(int width, int height)
{
    TRACE("WARN: stub. size=%ix%i", width, height);
    return NO_ERROR;
}

void HWComposerNativeWindow::lockBuffers(ANativeWindowBuffer **front, ANativeWindowBuffer **back)
{
	pthread_mutex_lock(&_mutex);
	while (m_frontBuf->rendered != 1)
	{
		printf("Needing to wait for buffer..\n");
	        pthread_cond_wait(&_cond, &_mutex);
	}
	*front = this->m_frontBuf;
	*back = this->m_backBuf;
	pthread_mutex_unlock(&_mutex);        
}

// vim: noai:ts=4:sw=4:ss=4:expandtab
