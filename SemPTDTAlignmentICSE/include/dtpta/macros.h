// DEV_PRINT macro for conditional debug output
#if DEV_MODE
#define DEV_PRINT(msg) std::cout << msg
#else
#define DEV_PRINT(msg)
#endif
