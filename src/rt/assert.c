//
//  Copyright (C) 2023  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "util.h"
#include "diag.h"
#include "jit/jit-exits.h"
#include "jit/jit.h"
#include "object.h"
#include "psl/psl-node.h"
#include "rt/model.h"
#include "rt/rt.h"
#include "rt/structs.h"
#include "tree.h"

#include <string.h>
#include <assert.h>
#include <float.h>
#include <stdlib.h>
#include <inttypes.h>

typedef struct _format_part format_part_t;

typedef enum {
   PART_TEXT,
   PART_REPLACEMENT
} part_kind_t;

typedef struct {
   char        variable;
   char        fill;
   char        align;
   int         width;
   uint64_t    precision;
   const char *units;
} replacement_t;

typedef struct _format_part {
   format_part_t *next;
   part_kind_t    kind;
   union {
      char          *text;
      replacement_t  rep;
   };
} format_part_t;

static const struct {
   const char *name;
   uint64_t    value;
} precision[] = {
   { "fs", 1 },
   { "ps", 1000 },
   { "ns", 1000000 },
   { "us", 1000000000 },
   { "ms", UINT64_C(1000000000000) },
   { "sec", UINT64_C(1000000000000000) },
   { "m", UINT64_C(60000000000000000) },
   { "min", UINT64_C(60000000000000000) },   // Non-standard
   { "hr", UINT64_C(3600000000000000000) },
};

static format_part_t *format[SEVERITY_FAILURE + 1];

static void free_format(format_part_t *f)
{
   for (format_part_t *tmp; f; f = tmp) {
      tmp = f->next;
      if (f->kind == PART_TEXT)
         free(f->text);
      free(f);
   }
}

static format_part_t *check_format(const char *str)
{
   // string_replacement ::= "{" variable [format_specification] "}"
   // format_specification ::= ":" [ [fill] align] [width] ["." precision]
   // variable ::= "s" | "S" | "r" | "t" | "i"
   // fill ::= graphic_character
   // align ::= "<" | ">" | "^"
   // precision ::= "fs" | "ps" | "ns" | "us" | "ms" | "sec" | "m" | "hr"

   format_part_t *result = NULL, **f = &result;
   const char *p = str, *start = str;

   while (*p) {
      if (*p++ != '{')
         continue;

      *f = xcalloc(sizeof(format_part_t));
      (*f)->kind = PART_TEXT;
      (*f)->text = xstrndup(start, p - start - 1);

      f = &((*f)->next);

      *f = xcalloc(sizeof(format_part_t));
      (*f)->kind = PART_REPLACEMENT;

      switch (((*f)->rep.variable = *p++)) {
      case 's': case 'S': case 'r': case 't': case 'i':
         break;
      default:
         errorf("invalid string replacement variable '%c'", (*f)->rep.variable);
         goto failed;
      }

      (*f)->rep.fill = ' ';
      (*f)->rep.align = ((*f)->rep.variable == 't' ? '>' : '<');

      if (*p == ':') {
         p++;

         int flen = 0;
         for (const char *pp = p; *pp && *pp != '}'; pp++)
            flen++;

         if (flen >= 2 && (p[1] == '<' || p[1] == '>' || p[1] == '^')) {
            // Fill and align
            (*f)->rep.fill = *p++;
            (*f)->rep.align = *p++;
         }
         else if (p[0] == '<' || p[0] == '>' || p[0] == '^') {
            // Align only
            (*f)->rep.fill = ' ';
            (*f)->rep.align = *p++;
         }

         if (isdigit_iso88591(*p))
            (*f)->rep.width = strtol(p, (char **)&p, 10);

         if (*p == '.') {
            p++;

            for (int i = 0; i < ARRAY_LEN(precision); i++) {
               const size_t len = strlen(precision[i].name);
               if (strncmp(p, precision[i].name, len) == 0) {
                  (*f)->rep.precision = precision[i].value;
                  (*f)->rep.units = precision[i].name;
                  p += len;
                  break;
               }
            }

            if ((*f)->rep.precision == 0) {
               errorf("invalid precision '%.*s' in format string",
                      (int)(strchrnul(p, '}') - p), p);
               goto failed;
            }
            else if ((*f)->rep.variable != 't') {
               errorf("precision is only valid for 't' variable");
               goto failed;
            }
         }
      }

      if (*p != '}') {
         errorf("expected '}' but found '%c' in format string", *p);
         goto failed;
      }

      start = ++p;
      f = &((*f)->next);
   }

   *f = xcalloc(sizeof(format_part_t));
   (*f)->kind = PART_TEXT;
   (*f)->text = xstrndup(start, p - start);

   return result;

 failed:
   free_format(result);
   return NULL;
}

DLLEXPORT
void _std_env_set_assert_format_valid(uint8_t level, const uint8_t *format_ptr,
                                      int64_t format_len, int8_t *valid)
{
   assert(level <= SEVERITY_FAILURE);

   char *cstr LOCAL = null_terminate(format_ptr, format_len);
   format_part_t *f = check_format(cstr);
   if (f == NULL)
      *valid = 0;
   else {
      free_format(format[level]);
      format[level] = f;
      *valid = 1;
   }
}

DLLEXPORT
void _std_env_set_assert_format(uint8_t level, const uint8_t *format_ptr,
                                int64_t format_len)
{
   assert(level <= SEVERITY_FAILURE);

   char *cstr LOCAL = null_terminate(format_ptr, format_len);
   format_part_t *f = check_format(cstr);
   if (f == NULL)
      jit_msg(NULL, DIAG_FATAL, "invalid assert format: %.*s",
              (int)format_len, format_ptr);

   free_format(format[level]);
   format[level] = f;
}

DLLEXPORT
void _std_env_get_assert_format(uint8_t level, ffi_uarray_t *u)
{
   assert(level <= SEVERITY_FAILURE);

   if (format[level] == NULL)
      *u = ffi_wrap(NULL, 1, 0);
   else {
      LOCAL_TEXT_BUF tb = tb_new();
      for (format_part_t *p = format[level]; p; p = p->next) {
         switch (p->kind) {
         case PART_TEXT:
            tb_cat(tb, p->text);
            break;
         case PART_REPLACEMENT:
            tb_append(tb, '{');
            tb_append(tb, p->rep.variable);
            tb_append(tb, ':');
            tb_append(tb, p->rep.fill);
            tb_append(tb, p->rep.align);

            if (p->rep.width > 0)
               tb_printf(tb, "%d", p->rep.width);

            if (p->rep.precision > 0) {
               for (int i = 0; i < ARRAY_LEN(precision); i++) {
                  if (p->rep.precision == precision[i].value) {
                     tb_printf(tb, ".%s", precision[i].name);
                     break;
                  }
               }
            }

            tb_append(tb, '}');
            break;
         }
      }

      const size_t nchars = tb_len(tb);
      char *mem = rt_tlab_alloc(nchars);
      memcpy(mem, tb_get(tb), nchars);
      *u = ffi_wrap(mem, 1, nchars);
   }
}

static const char *get_severity_string(vhdl_severity_t severity)
{
   static const char *levels[] = {
      "Note", "Warning", "Error", "Failure"
   };

   assert(severity < ARRAY_LEN(levels));
   return levels[severity];
}

static void apply_format(diag_t *d, format_part_t *p, vhdl_severity_t severity,
                         const uint8_t *msg, size_t msg_len)
{
   diag_clear(d);    // Suppress all default output
   diag_show_source(d, false);

   LOCAL_TEXT_BUF tb = tb_new();

   for (; p; p = p->next) {
      switch (p->kind) {
      case PART_TEXT:
         diag_printf(d, "%s", p->text);
         break;
      case PART_REPLACEMENT:
         {
            tb_rewind(tb);
            switch (p->rep.variable) {
            case 'r':
               tb_catn(tb, (const char *)msg, msg_len);
               break;
            case 's':
               tb_cat(tb, get_severity_string(severity));
               tb_downcase(tb);
               break;
            case 'S':
               tb_cat(tb, get_severity_string(severity));
               tb_upcase(tb);
               break;
            case 't':
               {
                  rt_model_t *model = get_model_or_null();
                  const uint64_t now = model ? model_now(model, NULL) : 0;

                  if (p->rep.precision == 0)
                     tb_printf(tb, "%"PRIi64" fs", now);
                  else {
                     const double frac = (double)now / (double)p->rep.precision;
                     tb_printf(tb, "%.*g %s", DBL_DIG, frac, p->rep.units);
                  }
               }
               break;
            case 'i':
               {
                  rt_proc_t *proc = get_active_proc();
                  if (proc != NULL) {
                     // The LRM says this produces the "instance path"
                     // but that is not defined anywhere
                     tree_t hier = tree_decl(proc->scope->where, 0);
                     assert(tree_kind(hier) == T_HIER);
                     tb_cat(tb, istr(tree_ident(hier)));
                  }
               }
               break;
            }

            const int tlen = tb_len(tb);

            int lpad = 0, rpad = 0;
            switch (p->rep.align) {
            case '>':
               lpad = p->rep.width - tlen;
               break;
            case '<':
               rpad = p->rep.width - tlen;
               break;
            case '^':
               lpad = (p->rep.width - tlen) / 2;
               rpad = p->rep.width - tlen - MAX(lpad, 0);
               break;
            }

            while (lpad-- > 0)
               diag_printf(d, "%c", p->rep.fill);

            diag_write(d, tb_get(tb), tlen);

            while (rpad-- > 0)
               diag_printf(d, "%c", p->rep.fill);
         }
         break;
      }
   }
}

void x_report(const uint8_t *msg, int32_t msg_len, int8_t severity,
              tree_t where)
{
   assert(severity <= SEVERITY_FAILURE);

   const diag_level_t level = diag_severity(severity);

   diag_t *d = diag_new(level, tree_loc(where));

   if (format[severity] != NULL)
      apply_format(d, format[severity], severity, msg, msg_len);
   else {
      diag_printf(d, "Report %s: ", get_severity_string(severity));
      diag_write(d, (const char *)msg, msg_len);
   }

   diag_show_source(d, false);
   diag_emit(d);

   if (level == DIAG_FATAL)
      jit_abort_with_status(EXIT_FAILURE);
}

void x_assert_fail(const uint8_t *msg, int32_t msg_len, int8_t severity,
                   int64_t hint_left, int64_t hint_right, int8_t hint_valid,
                   object_t *where)
{
   // LRM 93 section 8.2
   // The error message consists of at least
   // a) An indication that this message is from an assertion
   // b) The value of the severity level
   // c) The value of the message string
   // d) The name of the design unit containing the assertion

   assert(severity <= SEVERITY_FAILURE);

   const diag_level_t level = diag_severity(severity);

   diag_t *d = diag_new(level, &(where->loc));

   if (format[severity] != NULL)
      apply_format(d, format[severity], severity, msg, msg_len);
   else if (msg == NULL) {
      psl_node_t p = psl_from_object(where);
      if (p == NULL)
         diag_printf(d, "Assertion %s: Assertion violation.",
                     get_severity_string(severity));
      else
         diag_printf(d, "PSL assertion failed");
   }
   else {
      diag_printf(d, "Assertion %s: ", get_severity_string(severity));
      diag_write(d, (const char *)msg, msg_len);

      // Assume we don't want to dump the source code if the user
      // provided their own message
      diag_show_source(d, false);
   }

   if (hint_valid) {
      tree_t tree = tree_from_object(where);
      assert(tree != NULL);

      assert(tree_kind(tree) == T_FCALL);
      type_t p0_type = tree_type(tree_value(tree_param(tree, 0)));
      type_t p1_type = tree_type(tree_value(tree_param(tree, 1)));

      LOCAL_TEXT_BUF tb = tb_new();
      to_string(tb, p0_type, hint_left);
      switch (tree_subkind(tree_ref(tree))) {
      case S_SCALAR_EQ:  tb_cat(tb, " = "); break;
      case S_SCALAR_NEQ: tb_cat(tb, " /= "); break;
      case S_SCALAR_LT:  tb_cat(tb, " < "); break;
      case S_SCALAR_GT:  tb_cat(tb, " > "); break;
      case S_SCALAR_LE:  tb_cat(tb, " <= "); break;
      case S_SCALAR_GE:  tb_cat(tb, " >= "); break;
      }
      to_string(tb, p1_type, hint_right);
      tb_cat(tb, " is false");

      diag_hint(d, &(where->loc), "%s", tb_get(tb));
   }

   diag_emit(d);

   if (level == DIAG_FATAL)
      jit_abort_with_status(EXIT_FAILURE);
}
