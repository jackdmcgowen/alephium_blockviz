#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

// Define the Config struct
typedef struct {
    char* url;
} Config;

// Config array structure to hold multiple configs
typedef struct {
    Config* configs;
    int count;
} ConfigArray;

// Functions to load and free the config array
ConfigArray load_configs(const char* filename);
void free_configs(ConfigArray* config_array);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_H