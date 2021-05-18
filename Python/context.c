#include "Python.h"

#include "structmember.h"
#include "internal/pystate.h"
#include "internal/context.h"
#include "internal/hamt.h"
#include "internal/stackless_impl.h"
#include "internal/slp_prickelpit.h"


#define CONTEXT_FREELIST_MAXLEN 255
static PyContext *ctx_freelist = NULL;
static int ctx_freelist_len = 0;


#include "clinic/context.c.h"
/*[clinic input]
module _contextvars
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=a0955718c8b8cea6]*/


#define ENSURE_Context(o, err_ret)                                  \
    if (!PyContext_CheckExact(o)) {                                 \
        PyErr_SetString(PyExc_TypeError,                            \
                        "an instance of Context was expected");     \
        return err_ret;                                             \
    }

#define ENSURE_ContextVar(o, err_ret)                               \
    if (!PyContextVar_CheckExact(o)) {                              \
        PyErr_SetString(PyExc_TypeError,                            \
                       "an instance of ContextVar was expected");   \
        return err_ret;                                             \
    }

#define ENSURE_ContextToken(o, err_ret)                             \
    if (!PyContextToken_CheckExact(o)) {                            \
        PyErr_SetString(PyExc_TypeError,                            \
                        "an instance of Token was expected");       \
        return err_ret;                                             \
    }


/////////////////////////// Context API


static PyContext *
context_new_empty(void);

static PyContext *
context_new_from_vars(PyHamtObject *vars);

static inline PyContext *
context_get(void);

static PyContextToken *
token_new(PyContext *ctx, PyContextVar *var, PyObject *val);

static PyContextVar *
contextvar_new(PyObject *name, PyObject *def);

static int
contextvar_set(PyContextVar *var, PyObject *val);

static int
contextvar_del(PyContextVar *var);


PyObject *
_PyContext_NewHamtForTests(void)
{
    return (PyObject *)_PyHamt_New();
}


PyObject *
PyContext_New(void)
{
    return (PyObject *)context_new_empty();
}


PyObject *
PyContext_Copy(PyObject * octx)
{
    ENSURE_Context(octx, NULL)
    PyContext *ctx = (PyContext *)octx;
    return (PyObject *)context_new_from_vars(ctx->ctx_vars);
}


PyObject *
PyContext_CopyCurrent(void)
{
    PyContext *ctx = context_get();
    if (ctx == NULL) {
        return NULL;
    }

    return (PyObject *)context_new_from_vars(ctx->ctx_vars);
}


int
PyContext_Enter(PyObject *octx)
{
    ENSURE_Context(octx, -1)
    PyContext *ctx = (PyContext *)octx;

    if (ctx->ctx_entered) {
        PyErr_Format(PyExc_RuntimeError,
                     "cannot enter context: %R is already entered", ctx);
        return -1;
    }

    PyThreadState *ts = PyThreadState_GET();
    assert(ts != NULL);

    ctx->ctx_prev = (PyContext *)ts->context;  /* borrow */
    ctx->ctx_entered = 1;

    Py_INCREF(ctx);
    ts->context = (PyObject *)ctx;
    ts->context_ver++;

    return 0;
}


int
PyContext_Exit(PyObject *octx)
{
    ENSURE_Context(octx, -1)
    PyContext *ctx = (PyContext *)octx;

    if (!ctx->ctx_entered) {
        PyErr_Format(PyExc_RuntimeError,
                     "cannot exit context: %R has not been entered", ctx);
        return -1;
    }

    PyThreadState *ts = PyThreadState_GET();
    assert(ts != NULL);

    if (ts->context != (PyObject *)ctx) {
        /* Can only happen if someone misuses the C API */
        PyErr_SetString(PyExc_RuntimeError,
                        "cannot exit context: thread state references "
                        "a different context object");
        return -1;
    }

    Py_SETREF(ts->context, (PyObject *)ctx->ctx_prev);
    ts->context_ver++;

    ctx->ctx_prev = NULL;
    ctx->ctx_entered = 0;

    return 0;
}


PyObject *
PyContextVar_New(const char *name, PyObject *def)
{
    PyObject *pyname = PyUnicode_FromString(name);
    if (pyname == NULL) {
        return NULL;
    }
    PyContextVar *var = contextvar_new(pyname, def);
    Py_DECREF(pyname);
    return (PyObject *)var;
}


int
PyContextVar_Get(PyObject *ovar, PyObject *def, PyObject **val)
{
    ENSURE_ContextVar(ovar, -1)
    PyContextVar *var = (PyContextVar *)ovar;

    PyThreadState *ts = PyThreadState_GET();
    assert(ts != NULL);
    if (ts->context == NULL) {
        goto not_found;
    }

    if (var->var_cached != NULL &&
            var->var_cached_tsid == ts->id &&
            var->var_cached_tsver == ts->context_ver)
    {
        *val = var->var_cached;
        goto found;
    }

    assert(PyContext_CheckExact(ts->context));
    PyHamtObject *vars = ((PyContext *)ts->context)->ctx_vars;

    PyObject *found = NULL;
    int res = _PyHamt_Find(vars, (PyObject*)var, &found);
    if (res < 0) {
        goto error;
    }
    if (res == 1) {
        assert(found != NULL);
        var->var_cached = found;  /* borrow */
        var->var_cached_tsid = ts->id;
        var->var_cached_tsver = ts->context_ver;

        *val = found;
        goto found;
    }

not_found:
    if (def == NULL) {
        if (var->var_default != NULL) {
            *val = var->var_default;
            goto found;
        }

        *val = NULL;
        goto found;
    }
    else {
        *val = def;
        goto found;
   }

found:
    Py_XINCREF(*val);
    return 0;

error:
    *val = NULL;
    return -1;
}


PyObject *
PyContextVar_Set(PyObject *ovar, PyObject *val)
{
    ENSURE_ContextVar(ovar, NULL)
    PyContextVar *var = (PyContextVar *)ovar;

    if (!PyContextVar_CheckExact(var)) {
        PyErr_SetString(
            PyExc_TypeError, "an instance of ContextVar was expected");
        return NULL;
    }

    PyContext *ctx = context_get();
    if (ctx == NULL) {
        return NULL;
    }

    PyObject *old_val = NULL;
    int found = _PyHamt_Find(ctx->ctx_vars, (PyObject *)var, &old_val);
    if (found < 0) {
        return NULL;
    }

    Py_XINCREF(old_val);
    PyContextToken *tok = token_new(ctx, var, old_val);
    Py_XDECREF(old_val);

    if (contextvar_set(var, val)) {
        Py_DECREF(tok);
        return NULL;
    }

    return (PyObject *)tok;
}


int
PyContextVar_Reset(PyObject *ovar, PyObject *otok)
{
    ENSURE_ContextVar(ovar, -1)
    ENSURE_ContextToken(otok, -1)
    PyContextVar *var = (PyContextVar *)ovar;
    PyContextToken *tok = (PyContextToken *)otok;

    if (tok->tok_used) {
        PyErr_Format(PyExc_RuntimeError,
                     "%R has already been used once", tok);
        return -1;
    }

    if (var != tok->tok_var) {
        PyErr_Format(PyExc_ValueError,
                     "%R was created by a different ContextVar", tok);
        return -1;
    }

    PyContext *ctx = context_get();
    if (ctx != tok->tok_ctx) {
        PyErr_Format(PyExc_ValueError,
                     "%R was created in a different Context", tok);
        return -1;
    }

    tok->tok_used = 1;

    if (tok->tok_oldval == NULL) {
        return contextvar_del(var);
    }
    else {
        return contextvar_set(var, tok->tok_oldval);
    }
}


/////////////////////////// PyContext

/*[clinic input]
class _contextvars.Context "PyContext *" "&PyContext_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=bdf87f8e0cb580e8]*/


static inline PyContext *
_context_alloc(void)
{
    PyContext *ctx;
    if (ctx_freelist_len) {
        ctx_freelist_len--;
        ctx = ctx_freelist;
        ctx_freelist = (PyContext *)ctx->ctx_weakreflist;
        ctx->ctx_weakreflist = NULL;
        _Py_NewReference((PyObject *)ctx);
    }
    else {
        ctx = PyObject_GC_New(PyContext, &PyContext_Type);
        if (ctx == NULL) {
            return NULL;
        }
    }

    ctx->ctx_vars = NULL;
    ctx->ctx_prev = NULL;
    ctx->ctx_entered = 0;
    ctx->ctx_weakreflist = NULL;

    return ctx;
}


static PyContext *
context_new_empty(void)
{
    PyContext *ctx = _context_alloc();
    if (ctx == NULL) {
        return NULL;
    }

    ctx->ctx_vars = _PyHamt_New();
    if (ctx->ctx_vars == NULL) {
        Py_DECREF(ctx);
        return NULL;
    }

    _PyObject_GC_TRACK(ctx);
    return ctx;
}


static PyContext *
context_new_from_vars(PyHamtObject *vars)
{
    PyContext *ctx = _context_alloc();
    if (ctx == NULL) {
        return NULL;
    }

    Py_INCREF(vars);
    ctx->ctx_vars = vars;

    _PyObject_GC_TRACK(ctx);
    return ctx;
}


static inline PyContext *
context_get(void)
{
    PyThreadState *ts = PyThreadState_GET();
    assert(ts != NULL);
    PyContext *current_ctx = (PyContext *)ts->context;
    if (current_ctx == NULL) {
        current_ctx = context_new_empty();
        if (current_ctx == NULL) {
            return NULL;
        }
        ts->context = (PyObject *)current_ctx;
    }
    return current_ctx;
}

static int
context_check_key_type(PyObject *key)
{
    if (!PyContextVar_CheckExact(key)) {
        // abort();
        PyErr_Format(PyExc_TypeError,
                     "a ContextVar key was expected, got %R", key);
        return -1;
    }
    return 0;
}

static PyObject *
context_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    if (PyTuple_Size(args) || (kwds != NULL && PyDict_Size(kwds))) {
        PyErr_SetString(
            PyExc_TypeError, "Context() does not accept any arguments");
        return NULL;
    }
    return PyContext_New();
}

static int
context_tp_clear(PyContext *self)
{
    Py_CLEAR(self->ctx_prev);
    Py_CLEAR(self->ctx_vars);
    return 0;
}

static int
context_tp_traverse(PyContext *self, visitproc visit, void *arg)
{
    Py_VISIT(self->ctx_prev);
    Py_VISIT(self->ctx_vars);
    return 0;
}

static void
context_tp_dealloc(PyContext *self)
{
    _PyObject_GC_UNTRACK(self);

    if (self->ctx_weakreflist != NULL) {
        PyObject_ClearWeakRefs((PyObject*)self);
    }
    (void)context_tp_clear(self);

    if (ctx_freelist_len < CONTEXT_FREELIST_MAXLEN) {
        ctx_freelist_len++;
        self->ctx_weakreflist = (PyObject *)ctx_freelist;
        ctx_freelist = self;
    }
    else {
        Py_TYPE(self)->tp_free(self);
    }
}

static PyObject *
context_tp_iter(PyContext *self)
{
    return _PyHamt_NewIterKeys(self->ctx_vars);
}

static PyObject *
context_tp_richcompare(PyObject *v, PyObject *w, int op)
{
    if (!PyContext_CheckExact(v) || !PyContext_CheckExact(w) ||
            (op != Py_EQ && op != Py_NE))
    {
        Py_RETURN_NOTIMPLEMENTED;
    }

    int res = _PyHamt_Eq(
        ((PyContext *)v)->ctx_vars, ((PyContext *)w)->ctx_vars);
    if (res < 0) {
        return NULL;
    }

    if (op == Py_NE) {
        res = !res;
    }

    if (res) {
        Py_RETURN_TRUE;
    }
    else {
        Py_RETURN_FALSE;
    }
}

static Py_ssize_t
context_tp_len(PyContext *self)
{
    return _PyHamt_Len(self->ctx_vars);
}

static PyObject *
context_tp_subscript(PyContext *self, PyObject *key)
{
    if (context_check_key_type(key)) {
        return NULL;
    }
    PyObject *val = NULL;
    int found = _PyHamt_Find(self->ctx_vars, key, &val);
    if (found < 0) {
        return NULL;
    }
    if (found == 0) {
        PyErr_SetObject(PyExc_KeyError, key);
        return NULL;
    }
    Py_INCREF(val);
    return val;
}

static int
context_tp_contains(PyContext *self, PyObject *key)
{
    if (context_check_key_type(key)) {
        return -1;
    }
    PyObject *val = NULL;
    return _PyHamt_Find(self->ctx_vars, key, &val);
}


/*[clinic input]
_contextvars.Context.get
    key: object
    default: object = None
    /

Return the value for `key` if `key` has the value in the context object.

If `key` does not exist, return `default`. If `default` is not given,
return None.
[clinic start generated code]*/

static PyObject *
_contextvars_Context_get_impl(PyContext *self, PyObject *key,
                              PyObject *default_value)
/*[clinic end generated code: output=0c54aa7664268189 input=c8eeb81505023995]*/
{
    if (context_check_key_type(key)) {
        return NULL;
    }

    PyObject *val = NULL;
    int found = _PyHamt_Find(self->ctx_vars, key, &val);
    if (found < 0) {
        return NULL;
    }
    if (found == 0) {
        Py_INCREF(default_value);
        return default_value;
    }
    Py_INCREF(val);
    return val;
}


/*[clinic input]
_contextvars.Context.items

Return all variables and their values in the context object.

The result is returned as a list of 2-tuples (variable, value).
[clinic start generated code]*/

static PyObject *
_contextvars_Context_items_impl(PyContext *self)
/*[clinic end generated code: output=fa1655c8a08502af input=00db64ae379f9f42]*/
{
    return _PyHamt_NewIterItems(self->ctx_vars);
}


/*[clinic input]
_contextvars.Context.keys

Return a list of all variables in the context object.
[clinic start generated code]*/

static PyObject *
_contextvars_Context_keys_impl(PyContext *self)
/*[clinic end generated code: output=177227c6b63ec0e2 input=114b53aebca3449c]*/
{
    return _PyHamt_NewIterKeys(self->ctx_vars);
}


/*[clinic input]
_contextvars.Context.values

Return a list of all variables’ values in the context object.
[clinic start generated code]*/

static PyObject *
_contextvars_Context_values_impl(PyContext *self)
/*[clinic end generated code: output=d286dabfc8db6dde input=6c3d08639ba3bf67]*/
{
    return _PyHamt_NewIterValues(self->ctx_vars);
}


/*[clinic input]
_contextvars.Context.copy

Return a shallow copy of the context object.
[clinic start generated code]*/

static PyObject *
_contextvars_Context_copy_impl(PyContext *self)
/*[clinic end generated code: output=30ba8896c4707a15 input=ebafdbdd9c72d592]*/
{
    return (PyObject *)context_new_from_vars(self->ctx_vars);
}


#ifdef STACKLESS
PyObject* slp_context_run_callback(PyFrameObject *f, int exc, PyObject *result)
{
    PyCFrameObject *cf = (PyCFrameObject *)f;
    PyObject *context = cf->ob1;
    cf->ob1 = NULL;

    if (cf->i) {
        /* called by tasklet.context_run(...) */
        PyThreadState *ts = PyThreadState_GET();
        assert(ts);
        assert(NULL == context || PyContext_CheckExact(context));
        Py_XSETREF(ts->context, context);
        ts->context_ver++;
    } else {
        assert(PyContext_CheckExact(context));
        if (PyContext_Exit(context)) {
            Py_CLEAR(result);
        }
        Py_DECREF(context);
    }

    SLP_STORE_NEXT_FRAME(PyThreadState_GET(), cf->f_back);
    return result;
}

SLP_DEF_INVALID_EXEC(slp_context_run_callback)
#endif


static PyObject *
context_run(PyContext *self, PyObject *const *args,
            Py_ssize_t nargs, PyObject *kwnames)
{
    STACKLESS_GETARG();
    assert(NULL != self);

    if (nargs < 1) {
        PyErr_SetString(PyExc_TypeError,
                        "run() missing 1 required positional argument");
        return NULL;
    }

    if (PyContext_Enter((PyObject *)self)) {
        return NULL;
    }

#ifdef STACKLESS
    PyThreadState *ts = PyThreadState_GET();
    PyCFrameObject *f = NULL;
    if (stackless) {
        f = slp_cframe_new(slp_context_run_callback, 1);
        if (f == NULL)
            return NULL;
        Py_INCREF(self);
        f->ob1 = (PyObject *)self;
        assert(f->i == 0);
        SLP_SET_CURRENT_FRAME(ts, (PyFrameObject *)f);
        /* f contains the only counted reference to current frame. This reference
         * keeps the fame alive during the following _PyObject_FastCallKeywords().
         */
    }
#endif
    STACKLESS_PROMOTE_ALL();

    PyObject *call_result = _PyObject_FastCallKeywords(
        args[0], args + 1, nargs - 1, kwnames);

    STACKLESS_ASSERT();
#ifdef STACKLESS
    if (stackless && !STACKLESS_UNWINDING(call_result)) {
        /* required, because we added a C-frame */
        assert(f);
        assert((PyFrameObject *)f == SLP_CURRENT_FRAME(ts));
        SLP_STORE_NEXT_FRAME(ts, (PyFrameObject *)f);
        Py_DECREF(f);
        return STACKLESS_PACK(ts, call_result);
    }
    Py_XDECREF(f);
    if (STACKLESS_UNWINDING(call_result)) {
        return call_result;
    }
#endif

    if (PyContext_Exit((PyObject *)self)) {
        return NULL;
    }

    return call_result;
}


static PyMethodDef PyContext_methods[] = {
    _CONTEXTVARS_CONTEXT_GET_METHODDEF
    _CONTEXTVARS_CONTEXT_ITEMS_METHODDEF
    _CONTEXTVARS_CONTEXT_KEYS_METHODDEF
    _CONTEXTVARS_CONTEXT_VALUES_METHODDEF
    _CONTEXTVARS_CONTEXT_COPY_METHODDEF
    {"run", (PyCFunction)context_run, METH_FASTCALL | METH_KEYWORDS | METH_STACKLESS, NULL},
    {NULL, NULL}
};

static PySequenceMethods PyContext_as_sequence = {
    0,                                   /* sq_length */
    0,                                   /* sq_concat */
    0,                                   /* sq_repeat */
    0,                                   /* sq_item */
    0,                                   /* sq_slice */
    0,                                   /* sq_ass_item */
    0,                                   /* sq_ass_slice */
    (objobjproc)context_tp_contains,     /* sq_contains */
    0,                                   /* sq_inplace_concat */
    0,                                   /* sq_inplace_repeat */
};

static PyMappingMethods PyContext_as_mapping = {
    (lenfunc)context_tp_len,             /* mp_length */
    (binaryfunc)context_tp_subscript,    /* mp_subscript */
};

PyTypeObject PyContext_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "Context",
    sizeof(PyContext),
    .tp_methods = PyContext_methods,
    .tp_as_mapping = &PyContext_as_mapping,
    .tp_as_sequence = &PyContext_as_sequence,
    .tp_iter = (getiterfunc)context_tp_iter,
    .tp_dealloc = (destructor)context_tp_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_richcompare = context_tp_richcompare,
    .tp_traverse = (traverseproc)context_tp_traverse,
    .tp_clear = (inquiry)context_tp_clear,
    .tp_new = context_tp_new,
    .tp_weaklistoffset = offsetof(PyContext, ctx_weakreflist),
    .tp_hash = PyObject_HashNotImplemented,
};


/////////////////////////// ContextVar


static int
contextvar_set(PyContextVar *var, PyObject *val)
{
    var->var_cached = NULL;
    PyThreadState *ts = PyThreadState_Get();

    PyContext *ctx = context_get();
    if (ctx == NULL) {
        return -1;
    }

    PyHamtObject *new_vars = _PyHamt_Assoc(
        ctx->ctx_vars, (PyObject *)var, val);
    if (new_vars == NULL) {
        return -1;
    }

    Py_SETREF(ctx->ctx_vars, new_vars);

    var->var_cached = val;  /* borrow */
    var->var_cached_tsid = ts->id;
    var->var_cached_tsver = ts->context_ver;
    return 0;
}

static int
contextvar_del(PyContextVar *var)
{
    var->var_cached = NULL;

    PyContext *ctx = context_get();
    if (ctx == NULL) {
        return -1;
    }

    PyHamtObject *vars = ctx->ctx_vars;
    PyHamtObject *new_vars = _PyHamt_Without(vars, (PyObject *)var);
    if (new_vars == NULL) {
        return -1;
    }

    if (vars == new_vars) {
        Py_DECREF(new_vars);
        PyErr_SetObject(PyExc_LookupError, (PyObject *)var);
        return -1;
    }

    Py_SETREF(ctx->ctx_vars, new_vars);
    return 0;
}

static Py_hash_t
contextvar_generate_hash(void *addr, PyObject *name)
{
    /* Take hash of `name` and XOR it with the object's addr.

       The structure of the tree is encoded in objects' hashes, which
       means that sufficiently similar hashes would result in tall trees
       with many Collision nodes.  Which would, in turn, result in slower
       get and set operations.

       The XORing helps to ensure that:

       (1) sequentially allocated ContextVar objects have
           different hashes;

       (2) context variables with equal names have
           different hashes.
    */

    Py_hash_t name_hash = PyObject_Hash(name);
    if (name_hash == -1) {
        return -1;
    }

    Py_hash_t res = _Py_HashPointer(addr) ^ name_hash;
    return res == -1 ? -2 : res;
}

static PyContextVar *
contextvar_new(PyObject *name, PyObject *def)
{
    if (!PyUnicode_Check(name)) {
        PyErr_SetString(PyExc_TypeError,
                        "context variable name must be a str");
        return NULL;
    }

    PyContextVar *var = PyObject_GC_New(PyContextVar, &PyContextVar_Type);
    if (var == NULL) {
        return NULL;
    }

    var->var_hash = contextvar_generate_hash(var, name);
    if (var->var_hash == -1) {
        Py_DECREF(var);
        return NULL;
    }

    Py_INCREF(name);
    var->var_name = name;

    Py_XINCREF(def);
    var->var_default = def;

    var->var_cached = NULL;
    var->var_cached_tsid = 0;
    var->var_cached_tsver = 0;

    if (_PyObject_GC_MAY_BE_TRACKED(name) ||
            (def != NULL && _PyObject_GC_MAY_BE_TRACKED(def)))
    {
        PyObject_GC_Track(var);
    }
    return var;
}


/*[clinic input]
class _contextvars.ContextVar "PyContextVar *" "&PyContextVar_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=445da935fa8883c3]*/


static PyObject *
contextvar_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "default", NULL};
    PyObject *name;
    PyObject *def = NULL;

    if (!PyArg_ParseTupleAndKeywords(
            args, kwds, "O|$O:ContextVar", kwlist, &name, &def))
    {
        return NULL;
    }

    return (PyObject *)contextvar_new(name, def);
}

static int
contextvar_tp_clear(PyContextVar *self)
{
    Py_CLEAR(self->var_name);
    Py_CLEAR(self->var_default);
    self->var_cached = NULL;
    self->var_cached_tsid = 0;
    self->var_cached_tsver = 0;
    return 0;
}

static int
contextvar_tp_traverse(PyContextVar *self, visitproc visit, void *arg)
{
    Py_VISIT(self->var_name);
    Py_VISIT(self->var_default);
    return 0;
}

static void
contextvar_tp_dealloc(PyContextVar *self)
{
    PyObject_GC_UnTrack(self);
    (void)contextvar_tp_clear(self);
    Py_TYPE(self)->tp_free(self);
}

static Py_hash_t
contextvar_tp_hash(PyContextVar *self)
{
    return self->var_hash;
}

static PyObject *
contextvar_tp_repr(PyContextVar *self)
{
    _PyUnicodeWriter writer;

    _PyUnicodeWriter_Init(&writer);

    if (_PyUnicodeWriter_WriteASCIIString(
            &writer, "<ContextVar name=", 17) < 0)
    {
        goto error;
    }

    PyObject *name = PyObject_Repr(self->var_name);
    if (name == NULL) {
        goto error;
    }
    if (_PyUnicodeWriter_WriteStr(&writer, name) < 0) {
        Py_DECREF(name);
        goto error;
    }
    Py_DECREF(name);

    if (self->var_default != NULL) {
        if (_PyUnicodeWriter_WriteASCIIString(&writer, " default=", 9) < 0) {
            goto error;
        }

        PyObject *def = PyObject_Repr(self->var_default);
        if (def == NULL) {
            goto error;
        }
        if (_PyUnicodeWriter_WriteStr(&writer, def) < 0) {
            Py_DECREF(def);
            goto error;
        }
        Py_DECREF(def);
    }

    PyObject *addr = PyUnicode_FromFormat(" at %p>", self);
    if (addr == NULL) {
        goto error;
    }
    if (_PyUnicodeWriter_WriteStr(&writer, addr) < 0) {
        Py_DECREF(addr);
        goto error;
    }
    Py_DECREF(addr);

    return _PyUnicodeWriter_Finish(&writer);

error:
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}


/*[clinic input]
_contextvars.ContextVar.get
    default: object = NULL
    /

Return a value for the context variable for the current context.

If there is no value for the variable in the current context, the method will:
 * return the value of the default argument of the method, if provided; or
 * return the default value for the context variable, if it was created
   with one; or
 * raise a LookupError.
[clinic start generated code]*/

static PyObject *
_contextvars_ContextVar_get_impl(PyContextVar *self, PyObject *default_value)
/*[clinic end generated code: output=0746bd0aa2ced7bf input=30aa2ab9e433e401]*/
{
    if (!PyContextVar_CheckExact(self)) {
        PyErr_SetString(
            PyExc_TypeError, "an instance of ContextVar was expected");
        return NULL;
    }

    PyObject *val;
    if (PyContextVar_Get((PyObject *)self, default_value, &val) < 0) {
        return NULL;
    }

    if (val == NULL) {
        PyErr_SetObject(PyExc_LookupError, (PyObject *)self);
        return NULL;
    }

    return val;
}

/*[clinic input]
_contextvars.ContextVar.set
    value: object
    /

Call to set a new value for the context variable in the current context.

The required value argument is the new value for the context variable.

Returns a Token object that can be used to restore the variable to its previous
value via the `ContextVar.reset()` method.
[clinic start generated code]*/

static PyObject *
_contextvars_ContextVar_set(PyContextVar *self, PyObject *value)
/*[clinic end generated code: output=446ed5e820d6d60b input=c0a6887154227453]*/
{
    return PyContextVar_Set((PyObject *)self, value);
}

/*[clinic input]
_contextvars.ContextVar.reset
    token: object
    /

Reset the context variable.

The variable is reset to the value it had before the `ContextVar.set()` that
created the token was used.
[clinic start generated code]*/

static PyObject *
_contextvars_ContextVar_reset(PyContextVar *self, PyObject *token)
/*[clinic end generated code: output=d4ee34d0742d62ee input=ebe2881e5af4ffda]*/
{
    if (!PyContextToken_CheckExact(token)) {
        PyErr_Format(PyExc_TypeError,
                     "expected an instance of Token, got %R", token);
        return NULL;
    }

    if (PyContextVar_Reset((PyObject *)self, token)) {
        return NULL;
    }

    Py_RETURN_NONE;
}


static PyObject *
contextvar_cls_getitem(PyObject *self, PyObject *args)
{
    Py_RETURN_NONE;
}

static PyMemberDef PyContextVar_members[] = {
    {"name", T_OBJECT, offsetof(PyContextVar, var_name), READONLY},
    {NULL}
};

static PyMethodDef PyContextVar_methods[] = {
    _CONTEXTVARS_CONTEXTVAR_GET_METHODDEF
    _CONTEXTVARS_CONTEXTVAR_SET_METHODDEF
    _CONTEXTVARS_CONTEXTVAR_RESET_METHODDEF
    {"__class_getitem__", contextvar_cls_getitem,
        METH_VARARGS | METH_STATIC, NULL},
    {NULL, NULL}
};

PyTypeObject PyContextVar_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "ContextVar",
    sizeof(PyContextVar),
    .tp_methods = PyContextVar_methods,
    .tp_members = PyContextVar_members,
    .tp_dealloc = (destructor)contextvar_tp_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)contextvar_tp_traverse,
    .tp_clear = (inquiry)contextvar_tp_clear,
    .tp_new = contextvar_tp_new,
    .tp_free = PyObject_GC_Del,
    .tp_hash = (hashfunc)contextvar_tp_hash,
    .tp_repr = (reprfunc)contextvar_tp_repr,
};


/////////////////////////// Token

static PyObject * get_token_missing(void);


/*[clinic input]
class _contextvars.Token "PyContextToken *" "&PyContextToken_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=338a5e2db13d3f5b]*/


static PyObject *
token_tp_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyErr_SetString(PyExc_RuntimeError,
                    "Tokens can only be created by ContextVars");
    return NULL;
}

static int
token_tp_clear(PyContextToken *self)
{
    Py_CLEAR(self->tok_ctx);
    Py_CLEAR(self->tok_var);
    Py_CLEAR(self->tok_oldval);
    return 0;
}

static int
token_tp_traverse(PyContextToken *self, visitproc visit, void *arg)
{
    Py_VISIT(self->tok_ctx);
    Py_VISIT(self->tok_var);
    Py_VISIT(self->tok_oldval);
    return 0;
}

static void
token_tp_dealloc(PyContextToken *self)
{
    PyObject_GC_UnTrack(self);
    (void)token_tp_clear(self);
    Py_TYPE(self)->tp_free(self);
}

static PyObject *
token_tp_repr(PyContextToken *self)
{
    _PyUnicodeWriter writer;

    _PyUnicodeWriter_Init(&writer);

    if (_PyUnicodeWriter_WriteASCIIString(&writer, "<Token", 6) < 0) {
        goto error;
    }

    if (self->tok_used) {
        if (_PyUnicodeWriter_WriteASCIIString(&writer, " used", 5) < 0) {
            goto error;
        }
    }

    if (_PyUnicodeWriter_WriteASCIIString(&writer, " var=", 5) < 0) {
        goto error;
    }

    PyObject *var = PyObject_Repr((PyObject *)self->tok_var);
    if (var == NULL) {
        goto error;
    }
    if (_PyUnicodeWriter_WriteStr(&writer, var) < 0) {
        Py_DECREF(var);
        goto error;
    }
    Py_DECREF(var);

    PyObject *addr = PyUnicode_FromFormat(" at %p>", self);
    if (addr == NULL) {
        goto error;
    }
    if (_PyUnicodeWriter_WriteStr(&writer, addr) < 0) {
        Py_DECREF(addr);
        goto error;
    }
    Py_DECREF(addr);

    return _PyUnicodeWriter_Finish(&writer);

error:
    _PyUnicodeWriter_Dealloc(&writer);
    return NULL;
}

static PyObject *
token_get_var(PyContextToken *self)
{
    Py_INCREF(self->tok_var);
    return (PyObject *)self->tok_var;
}

static PyObject *
token_get_old_value(PyContextToken *self)
{
    if (self->tok_oldval == NULL) {
        return get_token_missing();
    }

    Py_INCREF(self->tok_oldval);
    return self->tok_oldval;
}

static PyGetSetDef PyContextTokenType_getsetlist[] = {
    {"var", (getter)token_get_var, NULL, NULL},
    {"old_value", (getter)token_get_old_value, NULL, NULL},
    {NULL}
};

PyTypeObject PyContextToken_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "Token",
    sizeof(PyContextToken),
    .tp_getset = PyContextTokenType_getsetlist,
    .tp_dealloc = (destructor)token_tp_dealloc,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,
    .tp_traverse = (traverseproc)token_tp_traverse,
    .tp_clear = (inquiry)token_tp_clear,
    .tp_new = token_tp_new,
    .tp_free = PyObject_GC_Del,
    .tp_hash = PyObject_HashNotImplemented,
    .tp_repr = (reprfunc)token_tp_repr,
};

static PyContextToken *
token_new(PyContext *ctx, PyContextVar *var, PyObject *val)
{
    PyContextToken *tok = PyObject_GC_New(PyContextToken, &PyContextToken_Type);
    if (tok == NULL) {
        return NULL;
    }

    Py_INCREF(ctx);
    tok->tok_ctx = ctx;

    Py_INCREF(var);
    tok->tok_var = var;

    Py_XINCREF(val);
    tok->tok_oldval = val;

    tok->tok_used = 0;

    PyObject_GC_Track(tok);
    return tok;
}


/////////////////////////// Token.MISSING


static PyObject *_token_missing;


typedef struct {
    PyObject_HEAD
} PyContextTokenMissing;


static PyObject *
context_token_missing_tp_repr(PyObject *self)
{
    return PyUnicode_FromString("<Token.MISSING>");
}


PyTypeObject PyContextTokenMissing_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    "Token.MISSING",
    sizeof(PyContextTokenMissing),
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_repr = context_token_missing_tp_repr,
};


static PyObject *
get_token_missing(void)
{
    if (_token_missing != NULL) {
        Py_INCREF(_token_missing);
        return _token_missing;
    }

    _token_missing = (PyObject *)PyObject_New(
        PyContextTokenMissing, &PyContextTokenMissing_Type);
    if (_token_missing == NULL) {
        return NULL;
    }

    Py_INCREF(_token_missing);
    return _token_missing;
}


///////////////////////////


int
PyContext_ClearFreeList(void)
{
    int size = ctx_freelist_len;
    while (ctx_freelist_len) {
        PyContext *ctx = ctx_freelist;
        ctx_freelist = (PyContext *)ctx->ctx_weakreflist;
        ctx->ctx_weakreflist = NULL;
        PyObject_GC_Del(ctx);
        ctx_freelist_len--;
    }
    return size;
}


void
_PyContext_Fini(void)
{
    Py_CLEAR(_token_missing);
    (void)PyContext_ClearFreeList();
    (void)_PyHamt_Fini();
}


int
_PyContext_Init(void)
{
    if (!_PyHamt_Init()) {
        return 0;
    }

    if ((PyType_Ready(&PyContext_Type) < 0) ||
        (PyType_Ready(&PyContextVar_Type) < 0) ||
        (PyType_Ready(&PyContextToken_Type) < 0) ||
        (PyType_Ready(&PyContextTokenMissing_Type) < 0))
    {
        return 0;
    }

    PyObject *missing = get_token_missing();
    if (PyDict_SetItemString(
        PyContextToken_Type.tp_dict, "MISSING", missing))
    {
        Py_DECREF(missing);
        return 0;
    }
    Py_DECREF(missing);

#ifdef STACKLESS
    if (slp_register_execute(&PyCFrame_Type, "context_run_callback",
        slp_context_run_callback, SLP_REF_INVALID_EXEC(slp_context_run_callback)) != 0)
    {
        return 0;
    }
#endif

    return 1;
}
