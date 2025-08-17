#ifndef SYSTEM_MANAGER_HPP
#define SYSTEM_MANAGER_HPP

#include "logger.hpp"

// System states
typedef enum {
    SYSTEM_STATE_UNINITIALIZED = 0,
    SYSTEM_STATE_INITIALIZING,
    SYSTEM_STATE_RUNNING,
    SYSTEM_STATE_ERROR,
    SYSTEM_STATE_SHUTDOWN
} system_state_t;

// Subsystem status
typedef enum {
    SUBSYSTEM_STATUS_UNINITIALIZED = 0,
    SUBSYSTEM_STATUS_INITIALIZING,
    SUBSYSTEM_STATUS_READY,
    SUBSYSTEM_STATUS_RUNNING,
    SUBSYSTEM_STATUS_ERROR,
    SUBSYSTEM_STATUS_SHUTDOWN
} subsystem_status_t;

// System manager interface
class SystemManager {
public:
    static SystemManager& getInstance();
    
    // System lifecycle
    bool initialize();
    void run();
    void shutdown();
    
    // System state management
    system_state_t getSystemState() const { return system_state_; }
    void setSystemState(system_state_t state);
    
    // Subsystem status tracking
    void setSubsystemStatus(subsystem_t subsystem, subsystem_status_t status);
    subsystem_status_t getSubsystemStatus(subsystem_t subsystem) const;
    
    // Health monitoring
    bool performHealthCheck();
    void handleCriticalError(const char* error, const char* context);
    
    // Memory monitoring
    void monitorMemory();
    bool isMemoryHealthy() const;
    
    // Performance monitoring
    void startPerformanceTimer(const char* operation);
    void endPerformanceTimer(const char* operation);
    
private:
    SystemManager() = default;
    ~SystemManager() = default;
    SystemManager(const SystemManager&) = delete;
    SystemManager& operator=(const SystemManager&) = delete;
    
    // Internal state
    system_state_t system_state_ = SYSTEM_STATE_UNINITIALIZED;
    subsystem_status_t subsystem_status_[SUBSYSTEM_COUNT];
    
    // Memory monitoring
    uint32_t initial_heap_ = 0;
    uint32_t min_heap_seen_ = UINT32_MAX;
    uint32_t last_memory_check_ = 0;
    
    // Performance monitoring
    struct {
        const char* operation;
        uint32_t start_time;
        bool active;
    } perf_timer_;
    
    // Health check intervals
    static const uint32_t MEMORY_CHECK_INTERVAL_MS = 5000;
    static const uint32_t HEALTH_CHECK_INTERVAL_MS = 10000;
    
    // Memory thresholds
    static const uint32_t CRITICAL_HEAP_THRESHOLD = 15000;
    static const uint32_t LOW_HEAP_THRESHOLD = 30000;
    
    // Internal methods
    bool initializeSubsystems();
    void logSystemStatus();
    bool recoverFromError();
};

// Global system manager access
#define SYSTEM_MGR SystemManager::getInstance()

#endif // SYSTEM_MANAGER_HPP
