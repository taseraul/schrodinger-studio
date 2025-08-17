#include "system_manager.hpp"
#include "esp_heap_caps.h"
#include "esp_system.h"

SystemManager& SystemManager::getInstance() {
    static SystemManager instance;
    return instance;
}

bool SystemManager::initialize() {
    LOG_SYSTEM_I("System Manager initialization starting...");
    setSystemState(SYSTEM_STATE_INITIALIZING);
    
    // Initialize all subsystem statuses
    for (int i = 0; i < SUBSYSTEM_COUNT; i++) {
        subsystem_status_[i] = SUBSYSTEM_STATUS_UNINITIALIZED;
    }
    
    // Record initial memory state
    initial_heap_ = ESP.getFreeHeap();
    min_heap_seen_ = initial_heap_;
    last_memory_check_ = millis();
    
    // Initialize performance timer
    perf_timer_.active = false;
    perf_timer_.operation = nullptr;
    perf_timer_.start_time = 0;
    
    log_memory_status("System Manager Init");
    
    // Initialize subsystems in order
    if (!initializeSubsystems()) {
        LOG_SYSTEM_E("Subsystem initialization failed");
        setSystemState(SYSTEM_STATE_ERROR);
        return false;
    }
    
    setSystemState(SYSTEM_STATE_RUNNING);
    LOG_SYSTEM_I("System Manager initialization completed successfully");
    logSystemStatus();
    
    return true;
}

void SystemManager::run() {
    if (system_state_ != SYSTEM_STATE_RUNNING) {
        return;
    }
    
    uint32_t current_time = millis();
    
    // Periodic memory monitoring
    if (current_time - last_memory_check_ >= MEMORY_CHECK_INTERVAL_MS) {
        monitorMemory();
        last_memory_check_ = current_time;
    }
    
    // Periodic health check
    static uint32_t last_health_check = 0;
    if (current_time - last_health_check >= HEALTH_CHECK_INTERVAL_MS) {
        if (!performHealthCheck()) {
            LOG_SYSTEM_W("Health check failed, attempting recovery");
            if (!recoverFromError()) {
                handleCriticalError("Health check recovery failed", "System run loop");
            }
        }
        last_health_check = current_time;
    }
}

void SystemManager::shutdown() {
    LOG_SYSTEM_I("System Manager shutdown initiated");
    setSystemState(SYSTEM_STATE_SHUTDOWN);
    
    // Set all subsystems to shutdown state
    for (int i = 0; i < SUBSYSTEM_COUNT; i++) {
        setSubsystemStatus((subsystem_t)i, SUBSYSTEM_STATUS_SHUTDOWN);
    }
    
    log_memory_status("System Shutdown");
    LOG_SYSTEM_I("System Manager shutdown completed");
}

void SystemManager::setSystemState(system_state_t state) {
    if (system_state_ != state) {
        const char* state_names[] = {
            "UNINITIALIZED", "INITIALIZING", "RUNNING", "ERROR", "SHUTDOWN"
        };
        
        LOG_SYSTEM_I("System state: %s -> %s", 
                     state_names[system_state_], state_names[state]);
        system_state_ = state;
    }
}

void SystemManager::setSubsystemStatus(subsystem_t subsystem, subsystem_status_t status) {
    if (subsystem >= SUBSYSTEM_COUNT) {
        LOG_SYSTEM_E("Invalid subsystem %d for status update", subsystem);
        return;
    }
    
    if (subsystem_status_[subsystem] != status) {
        const char* status_names[] = {
            "UNINITIALIZED", "INITIALIZING", "READY", "RUNNING", "ERROR", "SHUTDOWN"
        };
        
        log_subsystem_status(subsystem, status_names[status]);
        subsystem_status_[subsystem] = status;
    }
}

subsystem_status_t SystemManager::getSubsystemStatus(subsystem_t subsystem) const {
    if (subsystem >= SUBSYSTEM_COUNT) {
        return SUBSYSTEM_STATUS_ERROR;
    }
    return subsystem_status_[subsystem];
}

bool SystemManager::performHealthCheck() {
    // Check memory health
    if (!isMemoryHealthy()) {
        LOG_SYSTEM_W("Memory health check failed");
        return false;
    }
    
    // Check subsystem health
    int error_count = 0;
    for (int i = 0; i < SUBSYSTEM_COUNT; i++) {
        if (subsystem_status_[i] == SUBSYSTEM_STATUS_ERROR) {
            error_count++;
        }
    }
    
    if (error_count > 0) {
        LOG_SYSTEM_W("Health check: %d subsystems in error state", error_count);
        return false;
    }
    
    LOG_SYSTEM_D("Health check passed");
    return true;
}

void SystemManager::handleCriticalError(const char* error, const char* context) {
    setSystemState(SYSTEM_STATE_ERROR);
    log_error_with_context(LOG_TAG_SYSTEM, error, context);
    
    // Log current system status for debugging
    logSystemStatus();
    
    // Attempt recovery
    if (recoverFromError()) {
        LOG_SYSTEM_I("Recovery successful, resuming operation");
        setSystemState(SYSTEM_STATE_RUNNING);
    } else {
        LOG_SYSTEM_E("Recovery failed, system restart required");
        delay(1000);
        ESP.restart();
    }
}

void SystemManager::monitorMemory() {
    uint32_t current_heap = ESP.getFreeHeap();
    uint32_t current_psram = ESP.getFreePsram();
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    
    // Track minimum heap seen
    if (current_heap < min_heap_seen_) {
        min_heap_seen_ = current_heap;
    }
    
    // Calculate memory change from initial
    int32_t heap_change = (int32_t)current_heap - (int32_t)initial_heap_;
    
    // Log memory status
    LOG_MEMORY_I("Heap: %d (Δ%d), Min: %d, PSRAM: %d, Block: %d", 
                 current_heap, heap_change, min_heap_seen_, current_psram, largest_block);
    
    // Check for memory leaks
    if (heap_change < -15000) {
        LOG_MEMORY_W("Potential memory leak: %d bytes lost from baseline", -heap_change);
    }
    
    // Check for critical memory levels
    if (current_heap < CRITICAL_HEAP_THRESHOLD) {
        LOG_MEMORY_E("Critical memory level: %d bytes", current_heap);
        handleCriticalError("Critical memory shortage", "Memory monitor");
    } else if (current_heap < LOW_HEAP_THRESHOLD) {
        LOG_MEMORY_W("Low memory warning: %d bytes", current_heap);
    }
}

bool SystemManager::isMemoryHealthy() const {
    uint32_t current_heap = ESP.getFreeHeap();
    size_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    
    // Check for critical memory levels
    if (current_heap < CRITICAL_HEAP_THRESHOLD) {
        return false;
    }
    
    // Check for memory fragmentation
    if (largest_block < (current_heap / 2)) {
        LOG_MEMORY_W("Memory fragmentation detected");
        return false;
    }
    
    return true;
}

void SystemManager::startPerformanceTimer(const char* operation) {
    if (perf_timer_.active) {
        LOG_SYSTEM_W("Performance timer already active for '%s', overriding with '%s'", 
                     perf_timer_.operation, operation);
    }
    
    perf_timer_.operation = operation;
    perf_timer_.start_time = millis();
    perf_timer_.active = true;
}

void SystemManager::endPerformanceTimer(const char* operation) {
    if (!perf_timer_.active) {
        LOG_SYSTEM_W("No active performance timer for operation '%s'", operation);
        return;
    }
    
    uint32_t duration = millis() - perf_timer_.start_time;
    log_performance_metric(operation, duration);
    
    perf_timer_.active = false;
    perf_timer_.operation = nullptr;
}

bool SystemManager::initializeSubsystems() {
    LOG_SYSTEM_I("Initializing subsystems...");
    
    // Initialize logger first
    setSubsystemStatus(SUBSYSTEM_SYSTEM, SUBSYSTEM_STATUS_INITIALIZING);
    logger_init();
    setSubsystemStatus(SUBSYSTEM_SYSTEM, SUBSYSTEM_STATUS_RUNNING);
    
    // Memory subsystem is always running (no explicit init needed)
    setSubsystemStatus(SUBSYSTEM_MEMORY, SUBSYSTEM_STATUS_RUNNING);
    
    // Config subsystem will be initialized when config manager is created
    setSubsystemStatus(SUBSYSTEM_CONFIG, SUBSYSTEM_STATUS_READY);
    
    // Other subsystems will be initialized by their respective managers
    setSubsystemStatus(SUBSYSTEM_AUDIO, SUBSYSTEM_STATUS_UNINITIALIZED);
    setSubsystemStatus(SUBSYSTEM_NETWORK, SUBSYSTEM_STATUS_UNINITIALIZED);
    setSubsystemStatus(SUBSYSTEM_PROCESSING, SUBSYSTEM_STATUS_UNINITIALIZED);
    
    LOG_SYSTEM_I("Core subsystems initialized");
    return true;
}

void SystemManager::logSystemStatus() {
    LOG_SYSTEM_I("=== SYSTEM STATUS ===");
    LOG_SYSTEM_I("System State: %d", system_state_);
    
    const char* subsystem_names[] = {
        "System", "Audio", "Network", "Processing", "Memory", "Config"
    };
    
    for (int i = 0; i < SUBSYSTEM_COUNT; i++) {
        LOG_SYSTEM_I("%s: %d", subsystem_names[i], subsystem_status_[i]);
    }
    
    log_memory_status("System Status");
    LOG_SYSTEM_I("====================");
}

bool SystemManager::recoverFromError() {
    LOG_SYSTEM_I("Attempting system recovery...");
    
    // Force garbage collection
    ESP.getHeapSize();
    delay(100);
    
    // Check if memory situation improved
    if (isMemoryHealthy()) {
        LOG_SYSTEM_I("Memory recovery successful");
        return true;
    }
    
    LOG_SYSTEM_W("Recovery failed - memory still unhealthy");
    return false;
}
