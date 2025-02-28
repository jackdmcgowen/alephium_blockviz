#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>
#include "config.h"

#define MAX_TOKEN_NAME_SZ ( 128 )

  /* Macro to map a JSON field to a struct member */
#define MAP_JSON_FIELD(json_obj, struct_ptr, field) \
    do \
    { \
        cJSON* json_item = cJSON_GetObjectItem((json_obj), #field); \
        if (cJSON_IsString(json_item) && json_item->valuestring != NULL) \
        { \
            size_t len = strnlen(json_item->valuestring, MAX_TOKEN_NAME_SZ ); \
            (struct_ptr)->field = (char*)malloc(len + 1); \
            if ((struct_ptr)->field) \
            { \
                strncpy((struct_ptr)->field, json_item->valuestring, len+1 ); \
            } \
            else \
            { \
                fprintf(stderr, "Error: Failed to allocate memory for '%s'\n", #field); \
            } \
        } \
        else \
        { \
            fprintf(stderr, "Warning: Failed to map '%s' field\n", #field); \
        } \
    } while (0)


static char* read_file
    (
    const char* filename
    )
{
FILE* file = fopen(filename, "r");
if (!file)
    {
    perror("Failed to open file");
    return NULL;
    }

fseek(file, 0, SEEK_END);
long size = ftell(file);
fseek(file, 0, SEEK_SET);
char* buffer = (char*)malloc(size + 1);

if (!buffer)
    {
    fclose(file);
    perror("Failed to allocate memory");
    return NULL;
    }

fread(buffer, 1, size, file);
buffer[size] = '\0';
fclose(file);

return buffer;

}   /* read_file() */


ConfigArray load_configs
    (
    const char* filename
    )
{
ConfigArray config_array = { NULL, 0 };

  /*  Read the JSON file */
char* json_string = read_file(filename);
if (!json_string)
    {
    return config_array;
    }

  /* Parse JSON string with cJSON */
cJSON* root = cJSON_Parse(json_string);
if (root == NULL)
    {
    printf("Error parsing JSON: %s\n", cJSON_GetErrorPtr());
    free(json_string);
    return config_array;
    }

  /* Check if root is an array */
if (!cJSON_IsArray(root))
    {
    printf("Error: JSON root is not an array\n");
    cJSON_Delete(root);
    free(json_string);
    return config_array;
    }

  /* Get the number of items in the array */
int array_size = cJSON_GetArraySize(root);
if (array_size == 0)
    {
    cJSON_Delete(root);
    free(json_string);
    return config_array;
    }

  /* Allocate memory for configs */
config_array.configs = (Config*)calloc(array_size, sizeof(Config));
if (!config_array.configs)
    {
    perror("Failed to allocate memory for configs");
    cJSON_Delete(root);
    free(json_string);
    return config_array;
    }
config_array.count = array_size;

/* Iterate over the array and map each object */
cJSON* item;
int i = 0;
cJSON_ArrayForEach(item, root)
    {
    MAP_JSON_FIELD(item, &config_array.configs[i], url );
    i++;
    }

  /* Cleanup JSON resources */
cJSON_Delete(root);
free(json_string);

  /* Print the results */
if( config_array.count > 0 && config_array.configs )
    {
    printf( "Found %d config entries\n", config_array.count );
    for( int i = 0; i < config_array.count; i++ )
        {
        if( config_array.configs[i].url )
            {
            printf( "Config %d - URL: %s\n", i, config_array.configs[i].url );
            }
        else
            {
            printf( "Config %d - URL not found or invalid\n", i );
            }
        }
    }
else
    {
    printf("No configs loaded\n");
    }

return config_array;

}   /* load_configs() */


void free_configs
    (
    ConfigArray *config_array
    )
{
if (config_array && config_array->configs)
    {
    for (int i = 0; i < config_array->count; i++)
        {
        free( config_array->configs[i].url ); 
        }
    
    free( config_array->configs );
    config_array->configs = NULL;
    config_array->count = 0;
    }

}   /* free_configs() */
