#pragma once

#include <unistd.h>

typedef struct dict dict_t;

/*
 * Returns a new empty dictionary. It should be destroyed when not used anymore.
 */
dict_t *dict_create ();

/*
 * Frees a dictionary.
 */
void    dict_destroy (dict_t *dic);

/*
 * Put an element in a dictionary.  key is case insensitive, val is a string.
 *   If the key already exists, its value is updated.  If val is NULL, the pair
 *   is deleted.
 */
void    dict_put (dict_t *dic, const char *key, const char *val);

/*
 * Returns the value associated with key, or NULL if none.
 */
char   *dict_get (const dict_t *dic, const char *key);

/*
 * Delete the pair associated with key.
 */
void    dict_del (dict_t *dic, const char *key);

/*
 * Returns the size of the dict.
 */
static size_t  dict_size (const dict_t *dic);

/* NON-0 TERMINATED VALS ****************************************************************
 * Put and get for values that are not 0-terminated.
 */

/*
 * Same as dict_put, but indicates the length of value.
 */
void    dict_putn (dict_t *dic, const char *key, const char *val, size_t val_len);

/*
 * Same as dict_get, but sets *pval_len to the length of the value.  If a value
 * val was inserted with dict_put, then its length is strlen(val) + 1.
 */
char   *dict_getn (const dict_t *dic, const char *key, size_t *pval_len);


/* ITERATION ****************************************************************************
 * dict_foreach: iterate through a dictionary, with key and val bound
 * successively to each key/value pair.  The variable len is set to the length of the value.
 *
 * Example:
 *
 *   dict_foreach(dic,
 *                 {
 *                   printf ("%s %s\n", key, val);
 *                 });
 */
#define dict_foreach(dic, code)                                         \
  do {                                                                  \
    const dict_t *dict_dic = dic;                                       \
    for (const dict_list_t *dict_el = dict_dic->dic_list; dict_el; dict_el = dict_el->next) { \
      const char *key __attribute__((unused)) = dict_el->key;           \
      const char *val __attribute__((unused)) = dict_el->val;           \
      size_t len __attribute__((unused)) = dict_el->val_len;            \
      code;                                                             \
    }                                                                   \
  } while (0)


/* PRIVATE *******************************************************************
 * The rest of this file should be considered "private"; you should not access
 * the structures directly, but use the functions above.
 */
typedef struct dict_list {
    char *key, *val;
    size_t val_len;
    struct dict_list *next, *prev;
} dict_list_t;

typedef struct dict {
    dict_list_t *dic_list;
    size_t size;
} dict_t;

static inline size_t dict_size (const dict_t *dic) { return dic->size; }
