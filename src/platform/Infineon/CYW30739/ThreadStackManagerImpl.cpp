/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Provides an implementation of the ThreadStackManager object.
 *
 */
/* this file behaves like a config.h, comes first */
#include <platform/CHIPDeviceEvent.h>
#include <platform/internal/CHIPDeviceLayerInternal.h>

#include <platform/OpenThread/GenericThreadStackManagerImpl_OpenThread.hpp>

#include <lib/support/CHIPPlatformMemory.h>
#include <openthread-system.h>

namespace chip {
namespace DeviceLayer {

using namespace ::chip::DeviceLayer::Internal;

ThreadStackManagerImpl ThreadStackManagerImpl::sInstance;

CHIP_ERROR ThreadStackManagerImpl::_InitThreadStack()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    wiced_result_t result;

    mThread = wiced_rtos_create_thread();
    VerifyOrExit(mThread != nullptr, err = CHIP_ERROR_NO_MEMORY);

    ReturnErrorOnFailure(mEventFlags.Init());

    mMutex = wiced_rtos_create_mutex();
    VerifyOrExit(mMutex != nullptr, err = CHIP_ERROR_NO_MEMORY);

    result = wiced_rtos_init_mutex(mMutex);
    VerifyOrExit(result == WICED_SUCCESS, err = CHIP_ERROR_INTERNAL);
    otSysInit(0, NULL);

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT
    mSrpClearAllSemaphore = wiced_rtos_create_mutex();
    VerifyOrExit(mSrpClearAllSemaphore != nullptr, err = CHIP_ERROR_NO_MEMORY);

    result = wiced_rtos_init_mutex(mSrpClearAllSemaphore);
    VerifyOrExit(result == WICED_SUCCESS, err = CHIP_ERROR_INTERNAL);
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT

    err = GenericThreadStackManagerImpl_OpenThread<ThreadStackManagerImpl>::DoInit(NULL);

exit:
    return err;
}

void ThreadStackManagerImpl::SignalThreadActivityPending()
{
    mEventFlags.Set(kActivityPendingEventFlag);
}

__attribute__((section(".text_in_ram"))) void ThreadStackManagerImpl::SignalThreadActivityPendingFromISR()
{
    mEventFlags.Set(kActivityPendingFromISREventFlag);
}

CHIP_ERROR ThreadStackManagerImpl::_StartThreadTask()
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    wiced_result_t result;

    result = wiced_rtos_init_thread(mThread, CHIP_DEVICE_CONFIG_THREAD_TASK_PRIORITY, CHIP_DEVICE_CONFIG_THREAD_TASK_NAME,
                                    ThreadTaskMain, CHIP_DEVICE_CONFIG_THREAD_TASK_STACK_SIZE, this);
    VerifyOrExit(result == WICED_SUCCESS, err = CHIP_ERROR_NO_MEMORY);

exit:
    return err;
}

void ThreadStackManagerImpl::_LockThreadStack()
{
    const wiced_result_t result = wiced_rtos_lock_mutex(mMutex);
    VerifyOrReturn(result == WICED_SUCCESS, ChipLogError(DeviceLayer, "%s %x", __func__, result));
}

void ThreadStackManagerImpl::_UnlockThreadStack()
{
    const wiced_result_t result = wiced_rtos_unlock_mutex(mMutex);
    VerifyOrReturn(result == WICED_SUCCESS || result == WICED_NOT_OWNED, ChipLogError(DeviceLayer, "%s %x", __func__, result));
}

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT
void ThreadStackManagerImpl::_WaitOnSrpClearAllComplete()
{
    const wiced_result_t result = wiced_rtos_lock_mutex(mSrpClearAllSemaphore);
    VerifyOrReturn(result == WICED_SUCCESS, ChipLogError(DeviceLayer, "%s %x", __func__, result));
}

void ThreadStackManagerImpl::_NotifySrpClearAllComplete()
{
    const wiced_result_t result = wiced_rtos_unlock_mutex(mSrpClearAllSemaphore);
    VerifyOrReturn(result == WICED_SUCCESS || result == WICED_NOT_OWNED, ChipLogError(DeviceLayer, "%s %x", __func__, result));
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_THREAD_SRP_CLIENT
// ===== Methods that override the GenericThreadStackMa

void ThreadStackManagerImpl::ThreadTaskMain(void)
{
    while (true)
    {
        uint32_t flags = 0;
        if (mEventFlags.WaitAnyForever(flags) != CHIP_NO_ERROR)
        {
            continue;
        }

        LockThreadStack();
        ProcessThreadActivity();
        UnlockThreadStack();
    }
}

void ThreadStackManagerImpl::ThreadTaskMain(uint32_t arg)
{
    ChipLogDetail(DeviceLayer, "Thread task running");
    reinterpret_cast<ThreadStackManagerImpl *>(arg)->ThreadTaskMain();
}

} // namespace DeviceLayer
} // namespace chip

using namespace ::chip::DeviceLayer;

extern "C" void otTaskletsSignalPending(otInstance * p_instance)
{
    ThreadStackMgrImpl().SignalThreadActivityPending();
}

extern "C" __attribute__((section(".text_in_ram"))) void otSysEventSignalPending(void)
{
    ThreadStackMgrImpl().SignalThreadActivityPendingFromISR();
}

extern "C" void * otPlatCAlloc(size_t aNum, size_t aSize)
{
    return CHIPPlatformMemoryCalloc(aNum, aSize);
}

extern "C" void otPlatFree(void * aPtr)
{
    CHIPPlatformMemoryFree(aPtr);
}
