// host_test/mocks/mock_persistence_backend.hpp
#pragma once

#include <cstring>
#include <gmock/gmock.h>

#include "esp_err.h"
#include "interfaces/i_persistence_backend.hpp"
#include "nvs.h"

/**
 * Mock implementation of IPersistenceBackend for testing.
 * Simulates in-memory storage (RTC or NVS).
 */
class MockPersistenceBackend : public IPersistenceBackend
{
private:
    static constexpr size_t MAX_STORAGE = 1024;
    uint8_t storage_[MAX_STORAGE] = {};
    size_t stored_size_ = 0;

public:
    MOCK_METHOD(esp_err_t, load, (void* data, size_t size), (override));
    MOCK_METHOD(esp_err_t, save, (const void* data, size_t size), (override));

    // Helper: configure mock to use real in-memory storage
    void UseRealStorage()
    {
        ON_CALL(*this, load).WillByDefault([this](void* data, size_t size) {
            if (size > MAX_STORAGE || stored_size_ == 0)
                return ESP_ERR_NVS_INVALID_LENGTH;
            memcpy(data, storage_, size);
            return ESP_OK;
        });

        ON_CALL(*this, save).WillByDefault([this](const void* data, size_t size) {
            if (size > MAX_STORAGE)
                return ESP_ERR_NVS_INVALID_LENGTH;
            memcpy(storage_, data, size);
            stored_size_ = size;
            return ESP_OK;
        });
    }

    // Helper: get the stored data for verification
    const uint8_t* GetStoredData() const { return storage_; }
    size_t GetStoredSize() const { return stored_size_; }

    // Helper: clear storage
    void Clear()
    {
        memset(storage_, 0, MAX_STORAGE);
        stored_size_ = 0;
    }
};

/**
 * Real in-memory implementation for simple tests.
 * No mocking - just plain storage.
 */
class FakePersistenceBackend : public IPersistenceBackend
{
private:
    static constexpr size_t MAX_STORAGE = 1024;
    uint8_t storage_[MAX_STORAGE] = {};
    size_t stored_size_ = 0;

public:
    esp_err_t load(void* data, size_t size) override
    {
        if (size > MAX_STORAGE || stored_size_ == 0)
            return ESP_ERR_NVS_INVALID_LENGTH;
        memcpy(data, storage_, size);
        return ESP_OK;
    }

    esp_err_t save(const void* data, size_t size) override
    {
        if (size > MAX_STORAGE)
            return ESP_ERR_NVS_INVALID_LENGTH;
        memcpy(storage_, data, size);
        stored_size_ = size;
        return ESP_OK;
    }

    const uint8_t* GetStoredData() const { return storage_; }
    size_t GetStoredSize() const { return stored_size_; }
    void Clear()
    {
        memset(storage_, 0, MAX_STORAGE);
        stored_size_ = 0;
    }
};
