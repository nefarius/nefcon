#pragma once

#include <Windows.h>
#include <SetupAPI.h>

namespace nefarius::util
{
    class INFHandleGuard
    {
    public:
        // Constructor takes the HINF handle to manage
        explicit INFHandleGuard(HINF handle) : handle_(handle)
        {
        }

        // Destructor releases the HINF resource
        ~INFHandleGuard()
        {
            if (handle_ != INVALID_HANDLE_VALUE)
            {
                SetupCloseInfFile(handle_);
            }
        }

        // Disable copy constructor and copy assignment operator
        INFHandleGuard(const INFHandleGuard&) = delete;
        INFHandleGuard& operator=(const INFHandleGuard&) = delete;

        // Move constructor
        INFHandleGuard(INFHandleGuard&& other) noexcept : handle_(other.handle_)
        {
            other.handle_ = INVALID_HANDLE_VALUE;
        }

        // Move assignment operator
        INFHandleGuard& operator=(INFHandleGuard&& other) noexcept
        {
            if (this != &other)
            {
                // Release current resource
                if (handle_ != INVALID_HANDLE_VALUE)
                {
                    SetupCloseInfFile(handle_);
                }
                // Transfer ownership
                handle_ = other.handle_;
                other.handle_ = INVALID_HANDLE_VALUE;
            }
            return *this;
        }

        // Function to manually release the handle, if needed
        void release()
        {
            if (handle_ != INVALID_HANDLE_VALUE)
            {
                SetupCloseInfFile(handle_);
                handle_ = INVALID_HANDLE_VALUE;
            }
        }

        // Accessor for the handle
        HINF get() const
        {
            return handle_;
        }

        bool is_invalid() const
        {
            return handle_ == INVALID_HANDLE_VALUE;
        }

    private:
        HINF handle_;
    };

    class HDEVINFOHandleGuard
    {
    public:
        // Constructor takes the HINF handle to manage
        explicit HDEVINFOHandleGuard(HDEVINFO handle) : handle_(handle)
        {
        }

        // Destructor releases the HINF resource
        ~HDEVINFOHandleGuard()
        {
            if (handle_ != INVALID_HANDLE_VALUE)
            {
                SetupDiDestroyDeviceInfoList(handle_);
            }
        }

        // Disable copy constructor and copy assignment operator
        HDEVINFOHandleGuard(const HDEVINFOHandleGuard&) = delete;
        HDEVINFOHandleGuard& operator=(const HDEVINFOHandleGuard&) = delete;

        // Move constructor
        HDEVINFOHandleGuard(HDEVINFOHandleGuard&& other) noexcept : handle_(other.handle_)
        {
            other.handle_ = INVALID_HANDLE_VALUE;
        }

        // Move assignment operator
        HDEVINFOHandleGuard& operator=(HDEVINFOHandleGuard&& other) noexcept
        {
            if (this != &other)
            {
                // Release current resource
                if (handle_ != INVALID_HANDLE_VALUE)
                {
                    SetupDiDestroyDeviceInfoList(handle_);
                }
                // Transfer ownership
                handle_ = other.handle_;
                other.handle_ = INVALID_HANDLE_VALUE;
            }
            return *this;
        }

        // Function to manually release the handle, if needed
        void release()
        {
            if (handle_ != INVALID_HANDLE_VALUE)
            {
                SetupDiDestroyDeviceInfoList(handle_);
                handle_ = INVALID_HANDLE_VALUE;
            }
        }

        // Accessor for the handle
        HDEVINFO get() const
        {
            return handle_;
        }

        bool is_invalid() const
        {
            return handle_ == INVALID_HANDLE_VALUE;
        }

    private:
        HDEVINFO handle_;
    };
}
