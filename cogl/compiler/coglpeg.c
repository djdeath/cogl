#include <glib.h>

typedef enum {
  NODE_TYPE_SNIPPET,
  NODE_TYPE_TYPE,
  NODE_TYPE_FUNCTION,
  NODE_TYPE_BLOCK,
  NODE_TYPE_EXPRESSION,
  NODE_TYPE_CONDITON,
  NODE_TYPE_RETURN,
  NODE_TYPE_DISCARD,
  NODE_TYPE_VARIABLE,
  NODE_TYPE_VARIABLE_REF,
  NODE_TYPE_CONST_VALUE,
} NodeType;

typedef struct _Node Node;

struct _Node {
  NodeType type;

  Node *parent;
};

typedef struct {
  Node base;

  gchar *name;

  GList *types;

  GList *blocks;

  GList *functions;
} Snippet;

typedef enum {
  META_TYPE_BASE,
  META_TYPE_ARRAY,
  META_TYPE_STRUCT,
  META_TYPE_NAMED,
} MetaType;

typedef enum {
  TYPE_BASE_VOID,
  TYPE_BASE_FLOAT,
  TYPE_BASE_INT,
  TYPE_BASE_VEC2,
  TYPE_BASE_VEC3,
  TYPE_BASE_VEC4,
  TYPE_BASE_IMAGE1D,
  TYPE_BASE_IMAGE2D,
  TYPE_BASE_IMAGE3D,
  TYPE_BASE_TEXTURE1D,
  TYPE_BASE_TEXTURE2D,
  TYPE_BASE_TEXTURE3D,
} TypeBase;

typedef struct _Type {
  Node base;

  gchar *name;
  MetaType meta_type;

  GList *members; /* For structs or array  */
  gint n_elements;
  TypeBase type_base;
} Type;

typedef struct _Expression Expression;
typedef struct _Variable Variable;

struct _Expression {
  Node base;

  const gchar *op;

  GList *expressions;
};

struct _Variable {
  Node base;

  gchar *name;

  Type *type;
};

typedef struct {
  Node base;

  gchar *name;

  GList *instructions;
  GList *variables;
} Block;

typedef struct {
  Node base;

  gchar *name;

  Type *type;
  GList *arguments;
  Block *block;
} Function;

typedef struct {
  Node base;

  gchar *name;
} VariableRef;

typedef struct {
  Node base;

  gint value;
} ConstValue;

/**/

typedef struct {
  GList *snippets;

  Snippet *current_snippet;
  Function *current_function;
  GList *current_blocks;
} ParserCtx;

static Type *type_new(ParserCtx *ctx, const gchar *name)
{
  return NULL;
}

static Type *type_array_new(ParserCtx *ctx, Type *base_type, gint n)
{
  Type *type = g_new0(Type, 1);

  type->base.type = NODE_TYPE_TYPE;
  type->meta_type = META_TYPE_ARRAY;
  type->members = g_list_append(NULL, base_type);
  type->n_elements = n;

  ctx->current_snippet->types =
    g_list_prepend(ctx->current_snippet->types, type);

  return type;
}

static Type *type_struct_new(ParserCtx *ctx, GList *members)
{
  Type *type = g_new0(Type, 1);
  gint i;

  type->base.type = NODE_TYPE_TYPE;
  type->meta_type = META_TYPE_STRUCT;
  type->members = members;

  ctx->current_snippet->types =
    g_list_prepend(ctx->current_snippet->types, type);

  return type;
}

static Type *type_named_new(ParserCtx *ctx, const gchar *name, Type *indirect_type)
{
  Type *type = g_new0(Type, 1);
  gint i;

  type->base.type = NODE_TYPE_TYPE;
  type->meta_type = META_TYPE_NAMED;
  type->name = g_strdup(name);
  type->members = g_list_append(NULL, indirect_type);

  ctx->current_snippet->types =
    g_list_prepend(ctx->current_snippet->types, type);

  return type;
}

static Snippet *snippet_start(ParserCtx *ctx, const gchar *name)
{
  Snippet *snippet = g_new0(Snippet, 1);

  snippet->base.type = NODE_TYPE_SNIPPET;
  snippet->name = g_strdup(name);

  ctx->snippets = g_list_prepend(ctx->snippets, snippet);
  ctx->current_snippet = snippet;

  return snippet;
}

static void snippet_end(ParserCtx *ctx)
{
  ctx->current_snippet = NULL;
}

static Block *block_start(ParserCtx *ctx, const gchar *name)
{
  Block *block = g_new0(Block, 1);

  block->base.type = NODE_TYPE_BLOCK;

  if (name) {
    block->name = g_strdup(name);
    /* block->base.parent = ctx->current_snippet; */
    ctx->current_snippet->blocks =
      g_list_prepend(ctx->current_snippet->blocks, block);
  } else {

  }

  ctx->current_blocks = g_list_prepend(ctx->current_blocks, block);

  return block;
}

static void block_end(ParserCtx *ctx)
{
  ctx->current_blocks = g_list_delete_link(ctx->current_blocks, ctx->current_blocks);
}

static Variable *variable_new(ParserCtx *ctx, Type *type, const gchar *name)
{
  Variable *var = g_new0(Variable, 1);

  var->base.type = NODE_TYPE_VARIABLE;
  var->name = g_strdup(name);
  var->type = type;

  return var;
}

static Function *function_start(ParserCtx *ctx, Type *type, const gchar *name, GList *arguments)
{
  Function *function = g_new0(Function, 1);

  function->base.type = NODE_TYPE_FUNCTION;
  function->type = type;
  function->name = g_strdup(name);
  function->arguments = arguments;

  ctx->current_snippet->functions =
    g_list_prepend(ctx->current_snippet->functions, function);
  ctx->current_function = function;

  return function;
}

static void function_end(ParserCtx *ctx)
{
  ctx->current_function = NULL;
}

static Expression *expression_new2(const gchar *op, Expression *e1, Expression *e2)
{
  Expression *expression = g_new(Expression, 1);

  expression->base.type = NODE_TYPE_EXPRESSION;
  expression->op = g_strdup(op);
  expression->expressions = g_list_append(g_list_append(NULL, e1), e2);

  return expression;
}

static Expression *expression_new1(const gchar *op, Expression *e1)
{
  Expression *expression = g_new(Expression, 1);

  expression->base.type = NODE_TYPE_EXPRESSION;
  expression->op = g_strdup(op);
  expression->expressions = g_list_append(NULL, e1);

  return expression;
}

static VariableRef *variable_ref_new(const gchar *name)
{
  VariableRef *var_ref = g_new0(VariableRef, 1);

  var_ref->base.type = NODE_TYPE_VARIABLE_REF;
  var_ref->name = g_strdup(name);

  return var_ref;
}

static ConstValue *const_value_new(gint value)
{
  ConstValue *const_value = g_new(ConstValue, 1);

  const_value->base.type = NODE_TYPE_CONST_VALUE;
  const_value->value = value;

  return const_value;
}

static Expression *call_new(const gchar *name, GList *expressions)
{
  Expression *expression = g_new(Expression, 1);

  expression->base.type = NODE_TYPE_EXPRESSION;
  expression->op = g_strdup("()");
  expression->expressions = expressions;

  return expression;
}

union value {
  gint integer;
  gchar *string;
  GList *list;
  Type *type;
  Variable *var;
  Expression *exp;
  VariableRef *var_ref;
  ConstValue *value;
};

//#define YY_DEBUG 1
#define YY_CTX_LOCAL 1
#define YY_CTX_MEMBERS ParserCtx ctx;
#define YYSTYPE union value
#define YY_PARSE(T) static T

#include "cogl.leg.c"

int main()
{
  yycontext ctx = { 0, 0, };
  int ret = yyparse(&ctx);

  printf(ret ? "success\n" : "failure\n");

  return ret == 0;
}
