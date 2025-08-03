// upkg_hash.h
#ifndef UPKG_HASH_H
#define UPKG_HASH_H

#include <stddef.h> // For size_t
#include <stdbool.h>
#include "upkg_struct.h"

// --- Hash Table Struct Definitions ---
// A single node in the linked list for a hash table bucket
typedef struct Node {
    Pkginfo data;        // The Pkginfo struct for a package
    struct Node *next;   // Pointer to the next node in the list
} Node;

// The main hash table structure
typedef struct HashTable {
    Node **buckets;      // Array of pointers to the head of each linked list
    size_t size;         // The number of buckets (prime number)
    size_t count;        // The number of items in the table
} HashTable;

// --- Hash Table Configuration ---
#define INITIAL_HASH_TABLE_SIZE 16
#define GROW_LOAD_FACTOR_THRESHOLD 0.75
#define SHRINK_LOAD_FACTOR_THRESHOLD 0.25
#define MIN_HASH_TABLE_SIZE 8
#define MAX_SUGGESTIONS 10 // Max number of suggestions to return

// --- Global Variables ---
extern bool g_verbose_mode; // To control verbose output.
extern HashTable *upkg_main_hash_table;

// --- Function Prototypes for Hash Table Operations ---
HashTable* create_hash_table(size_t initial_size);
Pkginfo *search(HashTable *table, const char *name);
int add_package_deep_copy(HashTable *table, Pkginfo *source_info);
void removepkg(HashTable *table, const char *name);
void destroy_hash_table(HashTable *table);
void glob(HashTable *table);
void list(HashTable *table);
void status_search(HashTable *table, const char *name);
char **suggestions(HashTable *table, const char *name);
void print_suggestions(HashTable *table, const char *prefix);

// --- Logging Functions ---
void upkg_log_verbose(const char *format, ...);
void upkg_log_debug(const char *format, ...);

#endif // UPKG_HASH_H
