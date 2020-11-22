#include "dict.h"
#include <stdlib.h>
#include <string.h>

dict_t *dict_create () {
  dict_t *ret = malloc (sizeof (dict_t));
  ret->dic_list = NULL;
  ret->size = 0;
  return ret;
}

void dict_destroy (dict_t *dic) {
  dict_list_t *el = dic->dic_list;

  while (el) {
    dict_list_t *next = el->next;
    free (el->key);
    free (el->val);
    free (el);
    el = next;
  }
  dic->dic_list = NULL;
  free (dic);
}

static dict_list_t *dict_find_elt (const dict_t *dic, const char *key) {
  dict_list_t *el;

  for (el = dic->dic_list; el && (strcasecmp (el->key, key) != 0);
       el = el->next)
    ;
  return el;
}

void dict_putn (dict_t *dic, const char *key, const char *val, size_t val_len) {
  if (val == NULL) {
    dict_del (dic, key);
    return;
  }

  dict_list_t *el = dict_find_elt (dic, key);

  if (el) {
    free (el->val);
    el->val = malloc (val_len);
    el->val_len = val_len;
    memcpy (el->val, val, val_len);
    return;
  }

  el = malloc (sizeof (dict_list_t));

  el->key = strdup (key);
  el->val = malloc (val_len);
  el->val_len = val_len;
  memcpy (el->val, val, val_len);
  el->next = dic->dic_list;
  el->prev = NULL;
  if (dic->dic_list)
    dic->dic_list->prev = el;
  dic->dic_list = el;
  dic->size++;
}

void dict_put (dict_t *dic, const char *key, const char *val) {
  dict_putn (dic, key, val, val ? strlen (val) + 1 : 0);
}

char *dict_getn (const dict_t *dic, const char *key, size_t *pval_len) {
  dict_list_t *el = dict_find_elt (dic, key);

  if (pval_len && el)
    *pval_len = el->val_len;

  return el ? el->val : NULL;
}

char *dict_get (const dict_t *dic, const char *key) {
  return dict_getn (dic, key, NULL);
}

void dict_del (dict_t *dic, const char *key) {
  dict_list_t *el = dict_find_elt (dic, key);

  if (!el)
    return;
  free (el->key);
  free (el->val);
  if (el->prev)
    el->prev->next = el->next;
  else
    dic->dic_list = el->next;

  if (el->next)
    el->next->prev = el->prev;

  dic->size--;

  free (el);
}
