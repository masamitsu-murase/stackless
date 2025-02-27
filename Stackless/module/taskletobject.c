/******************************************************

  The Tasklet

 ******************************************************/

#include "Python.h"
#include "structmember.h"

#ifdef STACKLESS
#include "pycore_stackless.h"
#include "pycore_context.h"

/*[clinic input]
module _stackless
class _stackless.tasklet "PyTaskletObject *" "&PyTasklet_Type"
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=81570dcf604e6e6d]*/
#include "clinic/taskletobject.c.h"


/*
 * Convert C-bitfield
 */
Py_LOCAL_INLINE(PyTaskletFlagStruc)
tasklet_flags_from_integer(int flags) {
#if defined(SLP_USE_NATIVE_BITFIELD_LAYOUT) && SLP_USE_NATIVE_BITFIELD_LAYOUT
    PyTaskletFlagStruc f;
    Py_MEMCPY(&f, &flags, sizeof(f));
#else
    /* the portable way */
    PyTaskletFlagStruc f = {0, };
    SLP_SET_BITFIELD(SLP_TASKLET_FLAGS, f, flags, blocked);
    SLP_SET_BITFIELD(SLP_TASKLET_FLAGS, f, flags, atomic);
    SLP_SET_BITFIELD(SLP_TASKLET_FLAGS, f, flags, ignore_nesting);
    SLP_SET_BITFIELD(SLP_TASKLET_FLAGS, f, flags, autoschedule);
    SLP_SET_BITFIELD(SLP_TASKLET_FLAGS, f, flags, block_trap);
    SLP_SET_BITFIELD(SLP_TASKLET_FLAGS, f, flags, is_zombie);
    SLP_SET_BITFIELD(SLP_TASKLET_FLAGS, f, flags, pending_irq);
#endif
    Py_BUILD_ASSERT(sizeof(f) == sizeof(flags));
    return f;
}

Py_LOCAL_INLINE(int)
tasklet_flags_as_integer(PyTaskletFlagStruc flags) {
    int f;
    Py_BUILD_ASSERT(sizeof(f) == sizeof(flags));
#if defined(SLP_USE_NATIVE_BITFIELD_LAYOUT) && SLP_USE_NATIVE_BITFIELD_LAYOUT
    Py_MEMCPY(&f, &flags, sizeof(f));
#else
    /* the portable way */
    f = SLP_GET_BITFIELD(SLP_TASKLET_FLAGS, flags, blocked) |
            SLP_GET_BITFIELD(SLP_TASKLET_FLAGS, flags, atomic) |
            SLP_GET_BITFIELD(SLP_TASKLET_FLAGS, flags, ignore_nesting) |
            SLP_GET_BITFIELD(SLP_TASKLET_FLAGS, flags, autoschedule) |
            SLP_GET_BITFIELD(SLP_TASKLET_FLAGS, flags, block_trap) |
            SLP_GET_BITFIELD(SLP_TASKLET_FLAGS, flags, is_zombie) |
            SLP_GET_BITFIELD(SLP_TASKLET_FLAGS, flags, pending_irq);
#endif
    return f;
}

void
slp_current_insert(PyTaskletObject *task)
{
    PyThreadState *ts = task->cstate->tstate;
    PyTaskletObject **chain = &ts->st.current;
    assert(ts);

    SLP_CHAIN_INSERT(PyTaskletObject, chain, task, next, prev);
    ++ts->st.runcount;
}

void
slp_current_insert_after(PyTaskletObject *task)
{
    PyThreadState *ts = task->cstate->tstate;
    PyTaskletObject *hold = ts->st.current;
    PyTaskletObject **chain = &ts->st.current;
    assert(ts);

    *chain = hold->next;
    SLP_CHAIN_INSERT(PyTaskletObject, chain, task, next, prev);
    *chain = hold;
    ++ts->st.runcount;
}

void
slp_current_uninsert(PyTaskletObject *task)
{
    slp_current_remove_tasklet(task);
}

PyTaskletObject *
slp_current_remove(void)
{
    PyThreadState *ts = _PyThreadState_GET();
    PyTaskletObject **chain = &ts->st.current, *ret;

    /* Make sure that the tasklet belongs to this thread.
     * During interpreter shutdown '(*chain)->cstate->tstate' may be already NULL.
     * See function slp_kill_tasks_with_stacks() in stacklesseval.c
     */
    assert((*chain)->cstate->tstate == ts ||
           (*chain)->cstate->tstate == NULL);

    --ts->st.runcount;
    assert(ts->st.runcount >= 0);
    SLP_CHAIN_REMOVE(PyTaskletObject, chain, ret, next, prev);
    if (ts->st.runcount == 0)
        assert(ts->st.current == NULL);
    return ret;
}

void
slp_current_remove_tasklet(PyTaskletObject *task)
{
    PyThreadState *ts = task->cstate->tstate;
    PyTaskletObject **chain = &ts->st.current, *ret, *hold;

    /* Make sure the tasklet is scheduled.
     */
    assert(task->next != NULL);
    assert(task->prev != NULL);
    assert(ts != NULL);
    --ts->st.runcount;
    assert(ts->st.runcount >= 0);
    hold = ts->st.current;
    ts->st.current = task;
    SLP_CHAIN_REMOVE(PyTaskletObject, chain, ret, next, prev);
    if (hold != task)
        ts->st.current = hold;
    if (ts->st.runcount == 0)
        assert(ts->st.current == NULL);
}

void
slp_current_unremove(PyTaskletObject* task)
{
    PyThreadState *ts = task->cstate->tstate;
    slp_current_insert(task);
    ts->st.current = task;
}

/*
 * Determine if a tasklet has C stack, and thus needs to
 * be switched to (killed) before it can be deleted.
 * Tasklets without C stack (in a soft switched state)
 * need only be released.
 * If a tasklet's thread has been killed, but the
 * tasklet still lingers, it has no restorable c state and may
 * as well be thrown away.  But this may of course cause
 * problems elsewhere (why didn't the tasklet die when instructed to?)
 */
static int
tasklet_has_c_stack_and_thread(PyTaskletObject *t)
{
    /* The GC may call this function for a current tasklet.
     * Therefore we need the complete check. */
    return t->f.frame && t->cstate && t->cstate->tstate &&
        (t->cstate->tstate->st.current == t ? t->cstate->tstate->st.nesting_level : t->cstate->nesting_level) != 0;
}

static int
tasklet_traverse(PyTaskletObject *t, visitproc visit, void *arg)
{
    Py_VISIT(t->f.frame);
    Py_VISIT(t->tempval);
    Py_VISIT(t->cstate);
    Py_VISIT(t->exc_state.exc_type);
    Py_VISIT(t->exc_state.exc_value);
    Py_VISIT(t->exc_state.exc_traceback);
    Py_VISIT(t->context);
    Py_VISIT(t->profileobj);
    Py_VISIT(t->traceobj);
    return 0;
}

static void
tasklet_clear_frames(PyTaskletObject *t)
{
    /* release frame chain */
    Py_CLEAR(t->f.frame);
}

static inline void
exc_state_clear(_PyErr_StackItem *exc_state)
{
    PyObject *t, *v, *tb;
    t = exc_state->exc_type;
    v = exc_state->exc_value;
    tb = exc_state->exc_traceback;
    exc_state->exc_type = NULL;
    exc_state->exc_value = NULL;
    exc_state->exc_traceback = NULL;
    Py_XDECREF(t);
    Py_XDECREF(v);
    Py_XDECREF(tb);
}

static int
tasklet_clear(PyTaskletObject *t)
{
    tasklet_clear_frames(t);
    Py_CLEAR(t->tempval);
    Py_CLEAR(t->def_globals);
    Py_CLEAR(t->context);
    t->profilefunc = t->tracefunc = NULL;
    t->tracing = 0;
    Py_CLEAR(t->profileobj);
    Py_CLEAR(t->traceobj);

    /* unlink task from cstate */
    if (t->cstate != NULL && t->cstate->task == t)
        t->cstate->task = NULL;
    Py_CLEAR(t->cstate);

    exc_state_clear(&t->exc_state);

    /* Assert that the tasklet is at the end of the chain. */
    assert(t->exc_state.previous_item == NULL);
    /* Unlink the exc_info chain. There is no guarantee, that
     * the object t->exc_info points to still exists, because
     * the order of calls to tp_clear is undefined.
     */
    t->exc_info = &t->exc_state;
    return 0;
}

/*
 * the following function tries to ensure that a tasklet is
 * really killed. It is called in a context where we can't
 * afford that it will not be dead afterwards.
 * Reason: When clearing or resurrecting and killing, the
 * tasklet is in fact already dead, and the only case that
 * could revive it was that __del__ was defined.
 * But in the context of __del__, we can't do anything but rely
 * on proper destruction, since nobody will listen to an exception.
 */

static void
kill_finally (PyObject *ob)
{
    PyThreadState *ts = _PyThreadState_GET();
    PyTaskletObject *self = (PyTaskletObject *) ob;
    int is_mine = ts == self->cstate->tstate;
    int i;

    /* this could happen if we have a refcount bug, so catch it here.
    assert(self != ts->st.current);
    It also gets triggered on interpreter exit when we kill the tasks
    with stacks (PyStackless_kill_tasks_with_stacks) and there is no
    way to differentiate that case.. so it just gets commented out.
    */

    self->flags.is_zombie = 1;
    for (i=0; i<10 && self->f.frame != NULL; i++) {
        PyTasklet_Kill(self);
        if (!is_mine)
            return; /* will be killed elsewhere */
    }
}


/* destructing a tasklet without destroying it */
static void
tasklet_finalize(PyObject *self)
{
    PyTaskletObject *t;
    PyObject *error_type, *error_value, *error_traceback;

    assert(PyTasklet_Check(self));
    t = (PyTaskletObject *)self;

    /* Save the current exception, if any. */
    PyErr_Fetch(&error_type, &error_value, &error_traceback);

    if (tasklet_has_c_stack_and_thread(t)) {
        /*
         * we want to cleanly kill the tasklet in the case it
         * was forgotten.
         */
        kill_finally(self);
    }

    /* We must not free a C-stack, that is still somewhat alive. Instead we
     * add the current tasklet to gc.garbage. That's perfectly OK, because the
     * tasklet is still intact. Of course this grows a new reference to the
     * tasklet.
     */
    if (t->f.frame && t->cstate && t->cstate->task == t && Py_SIZE(t->cstate) != 0) {
        if (Py_VerboseFlag) {
            PySys_WriteStderr("# tasklet_finalize: warning: tasklet %p has a non zero C-stack.\n", (void*)t);
        }
        if (_PyRuntime.gc.garbage == NULL) {
            _PyRuntime.gc.garbage = PyList_New(0);
            if (_PyRuntime.gc.garbage == NULL)
                Py_FatalError("gc couldn't create gc.garbage list");
        }
        TASKLET_SETVAL(t, Py_None);  /* don't keep tempval alive */
        if (PyList_Append(_PyRuntime.gc.garbage, self) < 0)
            PyErr_WriteUnraisable(self);
    }

    /* Restore the saved exception. */
    PyErr_Restore(error_type, error_value, error_traceback);
}

static void
tasklet_dealloc(PyTaskletObject *t)
{
    if (PyTasklet_CheckExact(t)) {
        /* When ob is subclass of stackless.tasklet, finalizer is called from
         * subtype_dealloc.
         */
        if (PyObject_CallFinalizerFromDealloc((PyObject *)t) < 0) {
            // resurrected.
            return;
        }
    }

    PyObject_GC_UnTrack(t);

    if (t->tsk_weakreflist != NULL)
        PyObject_ClearWeakRefs((PyObject *)t);

    tasklet_clear(t);

    Py_TYPE(t)->tp_free((PyObject*)t);
}

PyTaskletObject *
PyTasklet_New(PyTypeObject *type, PyObject *func)
{
    if (type == NULL) {
        type = &PyTasklet_Type;
    }
    if (!PyType_IsSubtype(type, &PyTasklet_Type)) {
        PyErr_SetNone(PyExc_TypeError);
        return NULL;
    }
    if (func && func != Py_None)
        return (PyTaskletObject*)PyObject_CallFunctionObjArgs((PyObject*)type, func, NULL);
    else
        return (PyTaskletObject*)PyObject_CallFunction((PyObject*)type, NULL);
}

Py_LOCAL_INLINE(PyObject *)
_get_tasklet_context(PyTaskletObject *self)
{
    PyThreadState *ts = self->cstate->tstate;
    PyThreadState *cts = PyThreadState_Get();
    PyObject *ctx;
    assert(cts);

    /* Get the context for the tasklet *self.
     * If the tasklet has no context, set a new empty one.
     */
    if (ts && self == ts->st.current) {
        /* the tasklet *self is current */
        ctx = ts->context;
        if (NULL == ctx) {
            if (ts == cts) {
                /* *self belongs to the current thread. Call a C-API function, that
                 * initializes ts->context as a side effect */
                ctx = PyContext_CopyCurrent();
                if (NULL == ctx)
                    return NULL;
                Py_DECREF(ctx);
                ctx = ts->context;
                assert(NULL != ctx);
            } else {
                slp_runtime_error("The tasklet has no context and you can't set one from a foreign thread.");
            }
        }
    } else {
        /* the tasklet *self is not current */
        ctx = self->context;
        if (NULL == ctx) {
            ctx = PyContext_New();
            if (NULL == ctx)
                return NULL;
            self->context = ctx;
        }
    }
    Py_INCREF(ctx);
    return ctx;
}

Py_LOCAL_INLINE(int)
_tasklet_init_context(PyTaskletObject *task)
{
    PyThreadState *cts = PyThreadState_Get();
    assert(cts);

    PyObject *ctx = _get_tasklet_context(cts->st.current);
    if (NULL == ctx)
        return -1;

    PyObject *obj = _stackless_tasklet_set_context_impl(task, ctx);
    Py_DECREF(ctx);
    if (NULL == obj)
        return -1;
    Py_DECREF(obj);
    return 0;
}

static int
impl_tasklet_setup(PyTaskletObject *task, PyObject *args, PyObject *kwds, int insert);

int
PyTasklet_BindEx(PyTaskletObject *task, PyObject *func, PyObject *args, PyObject *kwargs)
{
    PyThreadState *ts = task->cstate->tstate;
    if (func == Py_None)
        func = NULL;
    if (args == Py_None)
        args = NULL;
    if (kwargs == Py_None)
        kwargs = NULL;

    if (func != NULL && !PyCallable_Check(func))
        TYPE_ERROR("tasklet function must be a callable or None", -1);
    if (args != NULL && !PyTuple_Check(args))
        TYPE_ERROR("tasklet args must be a tuple or None", -1);
    if (kwargs != NULL && !PyDict_Check(kwargs))
        TYPE_ERROR("tasklet kwargs must be a dictionary or None", -1);
    if (ts && ts->st.current == task) {
        RUNTIME_ERROR("can't (re)bind the current tasklet", -1);
    }
    if (PyTasklet_Scheduled(task)) {
        RUNTIME_ERROR("tasklet is scheduled", -1);
    }
    if (PyTasklet_GetNestingLevel(task)) {
        RUNTIME_ERROR("tasklet has C state on its stack", -1);
    }
    if (ts && task == ts->st.main && args == NULL && kwargs == NULL) {
        RUNTIME_ERROR("can't unbind the main tasklet", -1);
    }

    /*
     * Set the context to the current context. It can be changed later on.
     */
    if (func)
        if (_tasklet_init_context(task))
            return -1;

    tasklet_clear_frames(task);
    task->recursion_depth = 0;
    assert(task->flags.autoschedule == 0);  /* probably unused */
    assert(task->flags.blocked == 0);
    assert(task->f.frame == NULL);

    /* cstate is set by bind_tasklet_to_frame() later on */

    if ( args == NULL && kwargs == NULL) {
        /* just binding or unbinding the function */
        if (func == NULL)
            func = Py_None;
        TASKLET_SETVAL(task, func);
    } else {
        /* adding arguments.  Absence of func means leave tmpval alone */
        PyObject *old = NULL;
        int result;
        if (func != NULL) {
            TASKLET_CLAIMVAL(task, &old);
            TASKLET_SETVAL(task, func);
        }
        if (args == NULL) {
            args = PyTuple_New(0);
            if (args == NULL)
                goto err;
        } else
            Py_INCREF(args);
        if (kwargs == NULL) {
            kwargs = PyDict_New();
            if (kwargs == NULL) {
                Py_DECREF(args);
                goto err;
            }
        } else
            Py_INCREF(kwargs);
        result = impl_tasklet_setup(task, args, kwargs, 0);
        Py_DECREF(args);
        Py_DECREF(kwargs);
        if (result)
            goto err;
        Py_XDECREF(old);
        return 0;
err:
        if (old != NULL)
            TASKLET_SETVAL_OWN(task, old);
        return -1;
    }
    return 0;
}

PyTaskletObject *
PyTasklet_Bind(PyTaskletObject *task, PyObject *func)
{
    if(PyTasklet_BindEx(task, func, NULL, NULL))
        return NULL;
    Py_INCREF(task);
    return task;
}


PyDoc_STRVAR(tasklet_bind__doc__,
"bind(func=None, args=None, kwargs=None)\n\
Binding a tasklet to a callable object, and arguments.\n\
The callable is usually passed in to the constructor.\n\
In some cases, it makes sense to be able to re-bind a tasklet,\n\
after it has been run, in order to keep its identity.\n\
This function can also be used, in place of setup() or __call__()\n\
to supply arguments to the bound function.  The difference is that\n\
this will not cause the tasklet to become runnable.\n\
If all the argument are None, this method unbinds the tasklet.\n\
Note that a tasklet can only be (un)bound if it doesn't have C-state\n\
and is not scheduled and is not the current tasklet.\
");

static PyObject *
tasklet_bind(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *func = Py_None;
    PyObject *fargs = Py_None;
    PyObject *fkwargs = Py_None;
    char *kwds[] = {"func", "args", "kwargs", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOO:bind", kwds,
        &func, &fargs, &fkwargs))
        return NULL;
    if (PyTasklet_BindEx((PyTaskletObject *)self, func, fargs, fkwargs))
        return NULL;
    Py_INCREF(self);
    return self;
}

#define TASKLET_TUPLEFMT "iOiOOOOOiiOO"

static PyObject *
tasklet_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyThreadState *ts = _PyThreadState_GET();
    PyTaskletObject *t;

    /* we always need a cstate, so be sure to initialize */
    if (ts->st.initial_stub == NULL) {
        PyMethodDef def = {"__new__", (PyCFunction)(void(*)(void))tasklet_new, METH_VARARGS|METH_KEYWORDS};
        PyObject *retval;
        PyObject *func = PyCFunction_New(&def, (PyObject*)type);
        if (NULL == func)
            return NULL;
        if (NULL == args) {
            PyObject *arg = PyTuple_New(0);
            if (NULL == arg)
                retval = NULL;
            else {
                retval = PyStackless_Call_Main(func, arg, kwds);
                Py_DECREF(arg);
            }
        }
        else
            retval = PyStackless_Call_Main(func, args, kwds);
        Py_DECREF(func);
        return retval;
    }
    if (type == NULL)
        type = &PyTasklet_Type;
    t = (PyTaskletObject *) type->tp_alloc(type, 0);
    if (t == NULL)
        return NULL;
    memset(&t->flags, 0, sizeof(t->flags));
    memset(&t->exc_state, 0, sizeof(t->exc_state));
    t->exc_info = &t->exc_state;
    t->recursion_depth = 0;
    t->next = NULL;
    t->prev = NULL;
    t->f.frame = NULL;
    Py_INCREF(Py_None);
    t->tempval = Py_None;
    t->tsk_weakreflist = NULL;
    t->context = NULL;
    Py_INCREF(ts->st.initial_stub);
    t->cstate = ts->st.initial_stub;
    t->def_globals = PyEval_GetGlobals();
    Py_XINCREF(t->def_globals);
    if (ts != SLP_INITIAL_TSTATE(ts)) {
        /* make sure to kill tasklets with their thread */
        if (slp_ensure_linkage(t)) {
            Py_DECREF(t);
            return NULL;
        }
    }
    return (PyObject*) t;
}

static int
tasklet_init(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *result = tasklet_bind(self, args, kwds);
    if (NULL == result)
        return -1;
    Py_DECREF(result);
    return 0;
}


/* tasklet pickling support */

PyDoc_STRVAR(tasklet_reduce__doc__,
"Pickling a tasklet for later re-animation.\n\
Note that a tasklet can always be pickled, unless it is current.\n\
Whether it can be run after unpickling depends on the state of the\n\
involved frames. In general, you cannot run a frame with a C state.\
");

/*
Notes on pickling:
We get into trouble with the normal __reduce__ protocol, since
tasklets tend to have tasklets in tempval, and this creates
infinite recursion on pickling.
We therefore adopt the 3-element protocol of __reduce__, where
the third thing is the argument tuple for __setstate__.
Note that we don't use None as the second tuple.
As explained in 'Pickling and unpickling extension types', this
would call a property __basicnew__. This is more complicated,
since __basicnew__ has no parameters, and we need to track
the tasklet type.
The easiest solution was to just use an empty tuple, which causes
simply the tasklet() call without parameters.
*/

static PyObject *
tasklet_reduce(PyTaskletObject * t, PyObject *value)
{
    PyObject *tup = NULL, *lis = NULL;
    PyFrameObject *f;
    PyThreadState *ts = t->cstate->tstate;
    PyObject *exc_type, *exc_value, *exc_traceback, *exc_info;
    PyObject *context = NULL;
    int tracing, c_functions;
    PyObject *profileobj, *traceobj;

    if (value && !PyLong_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "__reduce_ex__ argument should be an integer");
        return NULL;
    }

    if (ts && t == ts->st.current)
        RUNTIME_ERROR("You cannot __reduce__ the tasklet which is"
                      " current.", NULL);
    lis = PyList_New(0);
    if (lis == NULL) goto err_exit;
    f = t->f.frame;
    while (f != NULL) {
        int ret;
        PyObject * frame_reducer = slp_reduce_frame(f);
        if (frame_reducer == NULL)
            goto err_exit;
        ret = PyList_Append(lis, frame_reducer);
        Py_DECREF(frame_reducer);
        if (ret)
            goto err_exit;
        f = f->f_back;
    }
    if (PyList_Reverse(lis)) goto err_exit;
    assert(t->cstate != NULL);

    if (t->exc_state.previous_item != NULL) {
        PyErr_SetString(PyExc_SystemError, "unexpected previous _PyErr_StackItem in tasklet");
        goto err_exit;
    }

    context = _get_tasklet_context(t);
    if (NULL == context)
        goto err_exit;

    if (ts && ts->st.pickleflags & SLP_PICKLEFLAGS_PRESERVE_TRACING_STATE) {
        c_functions = slp_encode_ctrace_functions(t->tracefunc, t->profilefunc);
        if (-1 == c_functions)
            goto err_exit;
        tracing = t->tracing;
        profileobj = t->profileobj;
        if (NULL == profileobj)
            profileobj = Py_None;
        traceobj = t->traceobj;
        if (NULL == traceobj)
            traceobj = Py_None;
    } else {
        c_functions = 0;
        tracing = 0;
        profileobj = Py_None;
        traceobj = Py_None;
    }

    assert(!ts || t->exc_info != &ts->exc_state);
    /* Because of the test a few lines above, it is guaranteed that t is not the current tasklet.
     * Therefore we can simplify the line
     *
     * exc_info = slp_get_obj_for_exc_state(ts && ts->st.current == t ? ts->exc_info : t->exc_info, ts)
     *
     * to
     */
    assert(!(ts && ts->st.current == t));
    exc_info = slp_get_obj_for_exc_state(t->exc_info);
    if (exc_info == NULL)
        goto err_exit;
    assert(exc_info != Py_None);
    if (exc_info == (PyObject *)t) {
        Py_INCREF(Py_None);
        Py_SETREF(exc_info, Py_None);
    }
    assert(!PyTasklet_Check(exc_info)); /* must be a generator, coro, asynccoro, ... */

    exc_type = t->exc_state.exc_type;
    exc_value = t->exc_state.exc_value;
    exc_traceback = t->exc_state.exc_traceback;
    if (exc_type == NULL) exc_type = Py_None;
    if (exc_value == NULL) exc_value = Py_None;
    if (exc_traceback == NULL) exc_traceback = Py_None;
    Py_INCREF(exc_type);
    Py_INCREF(exc_value);
    Py_INCREF(exc_traceback);
    tup = Py_BuildValue((ts && (ts->st.pickleflags & SLP_PICKLEFLAGS_PICKLE_CONTEXT)) ?
                        "(O()(" TASKLET_TUPLEFMT "O))" : "(O()(" TASKLET_TUPLEFMT "))",
                        Py_TYPE(t),
                        tasklet_flags_as_integer(t->flags),
                        t->tempval,
                        t->cstate->nesting_level,
                        lis,
                        exc_type,
                        exc_value,
                        exc_traceback,
                        exc_info,
                        tracing,
                        c_functions,
                        profileobj,
                        traceobj,
                        context
                        );
    Py_DECREF(exc_info);
    Py_DECREF(exc_type);
    Py_DECREF(exc_value);
    Py_DECREF(exc_traceback);
err_exit:
    Py_XDECREF(lis);
    Py_XDECREF(context);
    return tup;
}


PyDoc_STRVAR(tasklet_setstate__doc__,
"Tasklets are first created without parameters, and then __setstate__\n\
is called. This was necessary, since pickle has problems pickling\n\
extension types when they reference themselves.\
");

/* note that args is a tuple, although we use METH_O */

static PyObject *
tasklet_setstate(PyObject *self, PyObject *args)
{
    PyTaskletObject *t = (PyTaskletObject *) self;
    PyObject *tempval, *lis;
    int flags, nesting_level;
    PyObject *exc_type, *exc_value, *exc_traceback;
    PyObject *old_type, *old_value, *old_traceback;
    PyObject *exc_info_obj;
    int tracing, c_functions;
    PyObject *profileobj, *traceobj;
    PyObject *context = NULL;
    PyFrameObject *f;
    Py_ssize_t i, nframes;
    int j;

    assert(t && PyTasklet_Check(t));

    if (PyTasklet_Alive(t))
        RUNTIME_ERROR("tasklet is alive", NULL);

    if (!PyArg_ParseTuple(args, "iOiO!OOOOiiOO|O:tasklet",
                          &flags,
                          &tempval,
                          &nesting_level,
                          &PyList_Type, &lis,
                          &exc_type,
                          &exc_value,
                          &exc_traceback,
                          &exc_info_obj,
                          &tracing,
                          &c_functions,
                          &profileobj,
                          &traceobj,
                          &context))
        return NULL;

    if (Py_None == context)
        context = NULL;
    if (context != NULL && !PyContext_CheckExact(context))
        TYPE_ERROR("tasklet state[8] must be a contextvars.Context or None", NULL);

    nframes = PyList_GET_SIZE(lis);
    TASKLET_SETVAL(t, tempval);

    /* There is an unpickling race condition.  While it is rare,
     * sometimes tasklets get their setstate call after the
     * channel they are blocked on.  If this happens and we
     * do not account for it, they will be left in a broken
     * state where they are on the channels chain, but have
     * cleared their blocked flag.
     *
     * We will assume that the presence of a chain, can only
     * mean that the chain is that of a channel, rather than
     * that of the main tasklet/scheduler. And therefore
     * they can leave their blocked flag in place because the
     * channel would have set it.
     */
    j = t->flags.blocked;
    t->flags = tasklet_flags_from_integer(flags);
    if (t->next == NULL) {
        t->flags.blocked = 0;
    } else {
        t->flags.blocked = j;
    }

    /* t->nesting_level = nesting_level;
       XXX how do we handle this?
       XXX to be done: pickle the cstate without a ref to the task.
       XXX This should make it not runnable in the future.
     */
    if (nframes > 0) {
        PyFrameObject *back;
        f = (PyFrameObject *) PyList_GET_ITEM(lis, 0);

        /* slp_ensure_new_frame() returns a new ref */
        if ((f = slp_ensure_new_frame(f)) == NULL)
            return NULL;
        back = f;
        for (i=1; i<nframes; ++i) {
            f = (PyFrameObject *) PyList_GET_ITEM(lis, i);
            if ((f = slp_ensure_new_frame(f)) == NULL) {
                Py_DECREF(back);
                return NULL;
            }
            assert(f->f_back == NULL);
            f->f_back = back;
            back = f;
        }
        t->f.frame = f;
        if(NULL == context && _tasklet_init_context(t))
            return NULL;
    }

    /* profile and tracing */
    if ((c_functions & 1) || (Py_None != traceobj)) {
        /* trace setting requested */
        if (PySys_Audit("sys.settrace", NULL)) {
            return NULL;
        }
    }

    if ((c_functions & 2) || (Py_None != profileobj)) {
        /* profile setting requested */
        if (PySys_Audit("sys.setprofile", NULL)) {
            return NULL;
        }
    }

    if (c_functions & 1) {
        Py_tracefunc func = slp_get_sys_trace_func();
        if (NULL == func)
            return NULL;
        t->tracefunc = func;
    } else {
        t->tracefunc = NULL;
    }
    if (c_functions & 2) {
        Py_tracefunc func = slp_get_sys_profile_func();
        if (NULL == func)
            return NULL;
        t->profilefunc = func;
    } else {
        t->profilefunc = NULL;
    }
    if (Py_None != profileobj) {
        Py_INCREF(profileobj);
        Py_XSETREF(t->profileobj, profileobj);
    } else {
        Py_CLEAR(t->profileobj);
    }
    if (Py_None != traceobj) {
        Py_INCREF(traceobj);
        Py_XSETREF(t->traceobj, traceobj);
    } else {
        Py_CLEAR(t->traceobj);
    }
    t->tracing = tracing;

    /* context */
    if (context) {
        PyObject *obj = _stackless_tasklet_set_context_impl(t, context);
        if (NULL == obj)
            return NULL;
        Py_DECREF(obj);
    }

    /* walk frames again and calculate recursion_depth */
    for (f = t->f.frame; f != NULL; f = f->f_back) {
        if (PyFrame_Check(f) && f->f_executing != SLP_FRAME_EXECUTING_NO) {
            /*
             * we count running frames which *have* added
             * to recursion_depth
             */
            ++t->recursion_depth;
        }
    }

    old_type = t->exc_state.exc_type;
    old_value = t->exc_state.exc_value;
    old_traceback = t->exc_state.exc_traceback;
    if (exc_type != Py_None) {
        Py_INCREF(exc_type);
        t->exc_state.exc_type = exc_type;
    } else
        t->exc_state.exc_type = NULL;
    if (exc_value != Py_None) {
        Py_INCREF(exc_value);
        t->exc_state.exc_value = exc_value;
    } else
        t->exc_state.exc_value = NULL;
    if (exc_traceback != Py_None) {
        Py_INCREF(exc_traceback);
        t->exc_state.exc_traceback = exc_traceback;
    } else
        t->exc_state.exc_value = NULL;
    assert(t->exc_state.previous_item == NULL);

    /* t must not be current, otherwise we would have to assign to ts->exc_info_obj */
    assert(t != _PyThreadState_GET()->st.current);
    if (exc_info_obj == Py_None) {
        t->exc_info = &t->exc_state;
    } else {
        /* Check the preconditions for the next assignment.
         *
         * The cast in the assignment is OK, because all possible concrete types of exc_info_obj
         * have the exec_state member at the same offset.
         */
        assert(PyGen_Check(exc_info_obj) ||
                PyObject_TypeCheck(exc_info_obj, &PyCoro_Type) ||
                PyObject_TypeCheck(exc_info_obj, &PyAsyncGen_Type));
        Py_BUILD_ASSERT(offsetof(PyGenObject, gi_exc_state) == offsetof(PyCoroObject, cr_exc_state));
        Py_BUILD_ASSERT(offsetof(PyGenObject, gi_exc_state) == offsetof(PyAsyncGenObject, ag_exc_state));
        /* Make sure, that *exc_info_obj stays alive after Py_DECREF(args).
         */
        assert(Py_REFCNT(exc_info_obj) > 1);
        t->exc_info = &(((PyGenObject *)exc_info_obj)->gi_exc_state);
    }

    Py_INCREF(self);
    Py_XDECREF(old_type);
    Py_XDECREF(old_value);
    Py_XDECREF(old_traceback);
    return self;
}

PyDoc_STRVAR(tasklet_bind_thread__doc__,
"Attempts to re-bind the tasklet to the current thread.\n\
If the tasklet has non-trivial c state, a RuntimeError is\n\
raised.\n\
");


static PyObject *
tasklet_bind_thread(PyObject *self, PyObject *args)
{
    PyObject *thread_id = NULL;
    unsigned long target_tid = (unsigned long)-1;
    assert(PyTasklet_Check(self));
    if (!PyArg_ParseTuple(args, "|O!:bind_thread", &PyLong_Type, &thread_id))
        return NULL;
    if (!slp_parse_thread_id(thread_id, &target_tid))
        return NULL;
    if (PyTasklet_BindThread((PyTaskletObject *) self, target_tid))
        return NULL;
    Py_RETURN_NONE;
}

int
PyTasklet_BindThread(PyTaskletObject *task, unsigned long thread_id)
{
    PyThreadState *ts = task->cstate->tstate;
    PyThreadState *cts = _PyThreadState_GET();
    PyObject *old;
    assert(PyTasklet_Check(task));

    if (thread_id == (unsigned long)-1 && ts == cts)
        return 0; /* already bound to current thread*/

    if (PyTasklet_Scheduled(task) && !task->flags.blocked) {
        RUNTIME_ERROR("can't (re)bind a runnable tasklet", -1);
    }
    if (PyTasklet_GetNestingLevel(task)) {
        RUNTIME_ERROR("tasklet has C state on its stack", -1);
    }
    if (thread_id != (unsigned long)-1) {
        /* find the correct thread state */
        for(cts = PyInterpreterState_ThreadHead(cts->interp);
            cts != NULL;
            cts = PyThreadState_Next(cts))
        {
            if (cts->thread_id == thread_id)
                break;
        }
    }
    if (cts == NULL || cts->st.initial_stub == NULL) {
        PyErr_SetString(PyExc_ValueError, "bad thread");
        return -1;
    }
    old = (PyObject*)task->cstate;
    task->cstate = cts->st.initial_stub;
    Py_INCREF(task->cstate);
    Py_DECREF(old);
    return 0;
}

/* other tasklet methods */

PyDoc_STRVAR(tasklet_remove__doc__,
"Removing a tasklet from the runnables queue.\n\
Note: If this tasklet has a non-trivial C-state attached, Stackless\n\
will kill the tasklet when the containing thread terminates.\n\
Since this will happen in some unpredictable order, it may cause unwanted\n\
side-effects. Therefore it is recommended to either run tasklets to the\n\
end or to explicitly kill() them.\
");

static PyObject *
tasklet_remove(PyObject *self, PyObject *unused);

static int
PyTasklet_Remove_M(PyTaskletObject *task)
{
    PyObject *ret;
    PyMethodDef def = {"remove", (PyCFunction)tasklet_remove, METH_NOARGS};
    ret = PyStackless_CallCMethod_Main(&def, (PyObject *) task, NULL);
    return slp_return_wrapper_hard(ret);
}

static int
impl_tasklet_remove(PyTaskletObject *task)
{
    PyThreadState *ts = _PyThreadState_GET();

    assert(PyTasklet_Check(task));
    if (ts->st.main == NULL) return PyTasklet_Remove_M(task);
    assert(ts->st.current != NULL);

    /* now, operate on the correct thread state */
    ts = task->cstate->tstate;
    if (ts == NULL)
        return 0;

    if (task->flags.blocked)
        RUNTIME_ERROR("You cannot remove a blocked tasklet.", -1);
    if (task == ts->st.current)
        RUNTIME_ERROR("The current tasklet cannot be removed.", -1);
    if (task->next == NULL)
        return 0;
    slp_current_remove_tasklet(task);
    Py_DECREF(task);
    return 0;
}

int
PyTasklet_Remove(PyTaskletObject *task)
{
    return impl_tasklet_remove(task);
}

static PyObject *
tasklet_remove(PyObject *self, PyObject *unused)
{
    if (impl_tasklet_remove((PyTaskletObject*) self))
        return NULL;
    Py_INCREF(self);
    return self;
}


PyDoc_STRVAR(tasklet_insert__doc__,
"Insert this tasklet at the end of the scheduler list,\n\
given that it isn't blocked.\n\
Blocked tasklets need to be reactivated by channels.");

static PyObject *
tasklet_insert(PyObject *self, PyObject *unused);

static int
PyTasklet_Insert_M(PyTaskletObject *task)
{
    PyMethodDef def = {"insert", (PyCFunction)tasklet_insert, METH_NOARGS};
    PyObject *ret = PyStackless_CallCMethod_Main(&def, (PyObject *) task, NULL);
    return slp_return_wrapper_hard(ret);
}

static int
impl_tasklet_insert(PyTaskletObject *task)
{
    PyThreadState *ts = _PyThreadState_GET();

    assert(PyTasklet_Check(task));
    if (ts->st.main == NULL)
        return PyTasklet_Insert_M(task);
    if (task->flags.blocked)
        RUNTIME_ERROR("You cannot run a blocked tasklet", -1);
    if (task->next == NULL) {
        assert(task->cstate);
        if (task->cstate->tstate == NULL || task->cstate->tstate->st.main == NULL)
            RUNTIME_ERROR("Target thread isn't initialized", -1);
        if (task->f.frame == NULL && task != task->cstate->tstate->st.current)
            RUNTIME_ERROR("You cannot run an unbound(dead) tasklet", -1);
        Py_INCREF(task);
        slp_current_insert(task);
        /* The tasklet may belong to a different thread, and that thread may
         * be blocked, waiting for something to do!
         */
        slp_thread_unblock(task->cstate->tstate);
    }
    return 0;
}

int
PyTasklet_Insert(PyTaskletObject *task)
{
    return impl_tasklet_insert(task);
}

static PyObject *
tasklet_insert(PyObject *self, PyObject *unused)
{
    if (impl_tasklet_insert((PyTaskletObject*) self))
        return NULL;
    Py_INCREF(self);
    return self;
}


PyDoc_STRVAR(tasklet_run__doc__,
"Run this tasklet, given that it isn't blocked.\n\
Blocked tasks need to be reactivated by channels.");

static PyObject *
tasklet_run(PyObject *self, PyObject *unused);

static PyObject *
PyTasklet_Run_M(PyTaskletObject *task)
{
    PyMethodDef def = {"run", (PyCFunction)tasklet_run, METH_NOARGS};
    return PyStackless_CallCMethod_Main(&def, (PyObject *) task, NULL);
}

static PyObject *
impl_tasklet_run_remove(PyTaskletObject *task, int remove);

int
PyTasklet_Run_nr(PyTaskletObject *task)
{
    PyThreadState *ts = _PyThreadState_GET();
    STACKLESS_PROPOSE_ALL(ts);
    return slp_return_wrapper(impl_tasklet_run_remove(task, 0));
}

int
PyTasklet_Run(PyTaskletObject *task)
{
    return slp_return_wrapper_hard(impl_tasklet_run_remove(task, 0));
}

static PyObject *
PyTasklet_Switch_M(PyTaskletObject *task);

static PyObject *
impl_tasklet_run_remove(PyTaskletObject *task, int remove)
{
    STACKLESS_GETARG();
    PyThreadState *ts = _PyThreadState_GET();
    PyObject *ret;
    int inserted, fail, switched, removed=0;
    PyTaskletObject *prev = ts->st.current;

    assert(PyTasklet_Check(task));
    if (ts->st.main == NULL) {
        if (!remove)
            return PyTasklet_Run_M(task);
        else
            return PyTasklet_Switch_M(task);
    }


    /* we always call impl_tasklet_insert, so we must
     * also uninsert in case of failure
     */
    inserted = task->next == NULL;

    if (ts == task->cstate->tstate) {
        /* same thread behaviour.  Insert at the end of the queue and then
         * switch to that task.  Notice that this behaviour upsets FIFO
         * order
         */
        fail = impl_tasklet_insert(task);
        if (!fail && remove) {
            /* if we remove (tasklet.switch), then the current is effecively
             * replaced by the target.
             * move the reference of the current task to del_post_switch */
            assert(ts->st.del_post_switch == NULL);
            ts->st.del_post_switch = (PyObject*)prev;
            slp_current_remove();
            removed = 1;
        }
    } else {
        /* interthread. */
        PyThreadState *rts = task->cstate->tstate;
        PyTaskletObject *current;
        if (rts == NULL)
            RUNTIME_ERROR("tasklet has no thread", NULL);
        current = rts->st.current;
        /* switching only makes sense on the same thread. */
        if (remove)
            RUNTIME_ERROR("can't switch to a different thread.", NULL);

        if (rts->st.thread.is_idle) {
            /* remote thread is blocked, or unblocked and hasn't got the GIL yet.
             * insert it before the "current"
             */
            fail = impl_tasklet_insert(task);
            if (!fail)
                rts->st.current = task;
        } else if (rts->st.current) {
            /* remote thread is executing, put target after the current one */
            rts->st.current = rts->st.current->next;
            fail = impl_tasklet_insert(task);
            rts->st.current = current;
        } else {
            /* remote thread is in a weird state.  Just insert it */
            fail = impl_tasklet_insert(task);
        }
    }
    if (fail)
        return NULL;
    /* this is redundant in the interthread case, since insert already did the work */
    fail = slp_schedule_task(&ret, prev, task, stackless, &switched);
    if (fail) {
        if (removed) {
            assert(ts->st.del_post_switch == (PyObject *)prev);
            ts->st.del_post_switch = NULL;
            if (prev->next == NULL) /* in case of an error, the state is unknown */
                slp_current_unremove(prev);
        }
        if (inserted) {
            /* we must undo the insertion that we did, but we don't know the state */
            if (task->next != NULL) {
                slp_current_uninsert(task);
                Py_DECREF(task);
            }
        }
    } else if (!switched) {
        Py_CLEAR(ts->st.del_post_switch);
    }
    return ret;
}

static PyObject *
tasklet_run(PyObject *self, PyObject *unused)
{
    return impl_tasklet_run_remove((PyTaskletObject *) self, 0);
}

PyDoc_STRVAR(tasklet_switch__doc__,
"Similar to 'run', but additionally 'remove' the current tasklet\n\
atomically.  This primitive can be used to implement\n\
custom scheduling behaviour.  Only works for tasklets of the\n\
same thread.");

static PyObject *
tasklet_switch(PyObject *self, PyObject *unused);

static PyObject *
PyTasklet_Switch_M(PyTaskletObject *task)
{
    PyMethodDef def = {"switch", (PyCFunction)tasklet_switch, METH_NOARGS};
    return PyStackless_CallCMethod_Main(&def, (PyObject *) task, NULL);
}

int
PyTasklet_Switch_nr(PyTaskletObject *task)
{
    PyThreadState *ts = _PyThreadState_GET();
    STACKLESS_PROPOSE_ALL(ts);
    return slp_return_wrapper(impl_tasklet_run_remove(task, 1));
}

int
PyTasklet_Switch(PyTaskletObject *task)
{
    return slp_return_wrapper_hard(impl_tasklet_run_remove(task, 1));
}

static PyObject *
tasklet_switch(PyObject *self, PyObject *unused)
{
    return impl_tasklet_run_remove((PyTaskletObject *) self, 1);
}

PyDoc_STRVAR(tasklet_set_atomic__doc__,
"t.set_atomic(flag) -- set tasklet atomic status and return current one.\n\
If set, the tasklet will not be auto-scheduled.\n\
This flag is useful for critical sections which should not be interrupted.\n\
usage:\n\
    tmp = t.set_atomic(1)\n\
    # do critical stuff\n\
    t.set_atomic(tmp)\n\
Note: Whenever a new tasklet is created, the atomic flag is initialized\n\
with the atomic flag of the current tasklet.\
Atomic behavior is additionally influenced by the interpreter nesting level.\
See set_ignore_nesting.\
");

int
PyTasklet_SetAtomic(PyTaskletObject *task, int flag)
{
    int ret = task->flags.atomic;

    task->flags.atomic = flag ? 1 : 0;
    if (task->flags.pending_irq && task == _PyThreadState_GET()->st.current)
        slp_check_pending_irq();
    return ret;
}

static PyObject *
tasklet_set_atomic(PyObject *self, PyObject *flag)
{
    if (! (flag && PyLong_Check(flag)) )
        TYPE_ERROR("set_atomic needs exactly one bool or integer",
                   NULL);
    return PyBool_FromLong(PyTasklet_SetAtomic(
        (PyTaskletObject*)self, PyLong_AsLong(flag)));
}


PyDoc_STRVAR(tasklet_set_ignore_nesting__doc__,
"t.set_ignore_nesting(flag) -- set tasklet ignore_nesting status and return current one.\n\
If set, the tasklet may be be auto-scheduled, even if its nesting_level is > 0.\n\
This flag makes sense if you know that nested interpreter levels are safe\n\
for auto-scheduling. This is on your own risk, handle with care!\n\
usage:\n\
    tmp = t.set_ignore_nesting(1)\n\
    # do critical stuff\n\
    t.set_ignore_nesting(tmp)\
");

int
PyTasklet_SetIgnoreNesting(PyTaskletObject *task, int flag)
{
    int ret = task->flags.ignore_nesting;

    task->flags.ignore_nesting = flag ? 1 : 0;
    if (task->flags.pending_irq && task == _PyThreadState_GET()->st.current)
        slp_check_pending_irq();
    return ret;
}

static PyObject *
tasklet_set_ignore_nesting(PyObject *self, PyObject *flag)
{
    if (! (flag && PyLong_Check(flag)) )
        TYPE_ERROR("set_ignore_nesting needs exactly one bool or integer",
                   NULL);
    return PyBool_FromLong(PyTasklet_SetIgnoreNesting(
    (PyTaskletObject*)self, PyLong_AsLong(flag)));
}


static int
bind_tasklet_to_frame(PyTaskletObject *task, PyFrameObject *frame)
{
    PyThreadState *ts = task->cstate->tstate;
    if (ts == NULL)
        RUNTIME_ERROR("tasklet has no thread", -1);
    if (task->f.frame != NULL)
        RUNTIME_ERROR("tasklet is already bound to a frame", -1);
    task->f.frame = frame;
    if (task->cstate != ts->st.initial_stub) {
        PyCStackObject *hold = task->cstate;
        task->cstate = ts->st.initial_stub;
        Py_INCREF(task->cstate);
        Py_DECREF(hold);
        if (ts != SLP_INITIAL_TSTATE(ts))
            if (slp_ensure_linkage(task))
                return -1;
    }
    return 0;
    /* note: We expect that f_back is NULL, or will be adjusted immediately */
}

/* this is also the setup method */

PyDoc_STRVAR(tasklet_setup__doc__, "supply the parameters for the callable");

static int
PyTasklet_Setup_M(PyTaskletObject *task, PyObject *args, PyObject *kwds)
{
    PyObject *ret = PyStackless_Call_Main((PyObject*)task, args, kwds);

    return slp_return_wrapper(ret);
}

int PyTasklet_Setup(PyTaskletObject *task, PyObject *args, PyObject *kwds)
{
    PyObject *ret = Py_TYPE(task)->tp_call((PyObject *) task, args, kwds);

    return slp_return_wrapper(ret);
}

static int
impl_tasklet_setup(PyTaskletObject *task, PyObject *args, PyObject *kwds, int insert)
{
    PyThreadState *ts = _PyThreadState_GET();
    PyFrameObject *frame;
    PyObject *func;

    assert(PyTasklet_Check(task));
    if (ts->st.main == NULL) return PyTasklet_Setup_M(task, args, kwds);

    assert(task->recursion_depth == 0);
    assert(task->flags.is_zombie == 0);
    assert(task->flags.autoschedule == 0);  /* probably unused */
    assert(task->flags.blocked == 0);
    assert(task->f.frame == NULL);

    func = task->tempval;
    if (func == NULL || func == Py_None)
        RUNTIME_ERROR("the tasklet was not bound to a function", -1);
    if ((frame = (PyFrameObject *)
                 slp_cframe_newfunc(func, args, kwds, 0)) == NULL) {
        return -1;
    }
    if (bind_tasklet_to_frame(task, frame)) {
        /* bind_tasklet_to_frame steals one ref to frame on success */
        Py_DECREF(frame);
        return -1;
    }
    TASKLET_SETVAL(task, Py_None);
    if (insert) {
        Py_INCREF(task);
        slp_current_insert(task);
    }
    return 0;
}

static PyObject *
tasklet_setup(PyObject *self, PyObject *args, PyObject *kwds)
{
    PyTaskletObject *task = (PyTaskletObject *) self;

    if (PyTasklet_Alive(task)) {
        RUNTIME_ERROR("tasklet is alive", NULL);
    }

    /* The implementation of PyTasklet_Alive does not imply,
     * that the current tasklet is always alive. But I can't figure out,
     * how to create a current tasklet, that is dead.
     */
    assert(task->cstate->tstate == NULL || task->cstate->tstate->st.current != task);

    /* guaranted by !alive && !current.
     * Equivalent to the call of tasklet_clear_frames(task) in PyTasklet_BindEx().
     */
    assert(task->f.frame == NULL);

    /* the following assertions are equivalent to the remaining argument checks
     * in PyTasklet_BindEx().
     */
    assert(!PyTasklet_Scheduled(task));
    assert(PyTasklet_GetNestingLevel(task) == 0);

    if (impl_tasklet_setup(task, args, kwds, 1))
        return NULL;
    Py_INCREF(task);
    return (PyObject*) task;
}


PyDoc_STRVAR(tasklet_throw__doc__,
             "tasklet.throw(exc, val=None, tb=None, pending=False) -- raise an exception for the tasklet.\n\
             'exc', 'val' and 'tb' have the same semantics as the 'raise' statement of the Python(r) language.\n\
             If 'pending' is True, the tasklet is not immediately activated, just\n\
             merely made runnable, ready to raise the exception when run.");

static PyObject *
tasklet_throw(PyObject *myself, PyObject *args, PyObject *kwds);

static PyObject *
PyTasklet_Throw_M(PyTaskletObject *self, int pending, PyObject *exc,
                           PyObject *val, PyObject *tb)
{
    PyMethodDef def = {"throw", (PyCFunction)(void(*)(void))tasklet_throw, METH_VARARGS|METH_KEYWORDS};
    if (!val)
        val = Py_None;
    if (!tb)
        tb = Py_None;
    return PyStackless_CallCMethod_Main(&def, (PyObject *) self, "OOOi",  exc, val, tb, pending);
}

static PyObject *
#if PY_VERSION_HEX < 0x03030700
#define SLP_IMPL_THROW_BOMB_WITH_BOE 1
_impl_tasklet_throw_bomb(PyTaskletObject *self, int pending, PyObject *bomb, int bomb_on_error)
#else
#undef SLP_IMPL_THROW_BOMB_WITH_BOE
_impl_tasklet_throw_bomb(PyTaskletObject *self, int pending, PyObject *bomb)
#endif
{
    STACKLESS_GETARG();
    PyThreadState *ts = _PyThreadState_GET();
    PyObject *ret, *tmpval;
    int fail;

    assert(bomb != NULL);
    assert(PyBomb_Check(bomb));
    /* raise it directly if target is ourselves.  delayed exception makes
     * no sense in this case
     */
    if (ts->st.current == self) {
        assert(self->cstate->tstate == ts);
        return slp_bomb_explode(bomb);
    }

    /* Handle new or dead tasklets.
     */
    if (slp_get_frame(self) == NULL) {
        /* The tasklet is not alive.
         * There are a few special cases:
         *  - The purpose of raising exception TaskletExit is to end the tasklet. Therefore
         *    it is no error, if the tasklet already run to its end.
         *  - Otherwise we have to raise a RuntimeError.
         */
        if (!PyObject_IsSubclass(((PyBombObject*)bomb)->curexc_type, PyExc_TaskletExit) ||
            (self->cstate->tstate == NULL && self->f.frame != NULL)) {
            /* Error: the exception is not TaskletExit or the tasklet did not run to its end. */
#ifdef SLP_IMPL_THROW_BOMB_WITH_BOE
            if (bomb_on_error)
                return slp_bomb_explode(bomb);
#endif
            Py_DECREF(bomb);
            if (self->cstate->tstate == NULL) {
                RUNTIME_ERROR("tasklet has no thread", NULL);
            }
            RUNTIME_ERROR("You cannot throw to a dead tasklet", NULL);
        }
        /* A TaskletExit exception. The tasklet already ended (== can't be
         * resurrected by bind_thread).
         * Simply end the tasklet.
         */
        /* next two if()... just for test coverage mesurement */
        if (self->cstate->tstate != NULL) {
            assert(self->cstate->tstate != NULL);
        }
        if (self->f.frame == NULL) {
            assert(self->f.frame == NULL);
        }
        Py_DECREF(bomb);

        /* Now obey the post conditions of tasklet.throw:
         * 1. the tasklet is not blocked
         */
        if (self->next && self->flags.blocked) {
            /* we claim the channel's reference */
            slp_channel_remove_slow(self, NULL, NULL, NULL);
        } else {
            Py_INCREF(self);
        }
        /* Obey the post conditions of throw():
         * 2. the tasklet is not scheduled. This is also a precondition,
         *    because a dead tasklet must not be scheduled.
         */

#if 0   /* disabled until https://github.com/stackless-dev/stackless/issues/81 is resolved */
        assert(self->next == NULL && self->prev == NULL);
#endif

        /* Due to bugs the above assertion may not hold.
         * Try to work around.
         */
        if (self->next) {
            if (self->cstate->tstate != NULL) {
                /* The tasklet has a tstate an is scheduled.
                 * we can use the regular remove.
                 */
                slp_current_remove_tasklet(self);
            } else {
                /* The tasklet has no tstate, is not blocked on a channel.
                 * This happens, if a thread ended, but the tasklet was
                 * survived killing.
                 */
                SLP_HEADCHAIN_REMOVE(self, prev, next);
            }
            Py_DECREF(self);
        }
        Py_DECREF(self);  /* the ref from the channel */
        Py_RETURN_NONE;
    }
    assert(self->cstate->tstate != NULL);
    /* don't modify a tasklet on an uninitialised or dead thread */
    if (pending && self->cstate->tstate->st.main == NULL) {
#ifdef SLP_IMPL_THROW_BOMB_WITH_BOE
        if (bomb_on_error)
            return slp_bomb_explode(bomb);
#endif
        Py_DECREF(bomb);
        RUNTIME_ERROR("Target thread isn't initialised", NULL);
    }

    TASKLET_CLAIMVAL(self, &tmpval);
    TASKLET_SETVAL_OWN(self, bomb);
    if (!pending) {
        fail = slp_schedule_task(&ret, ts->st.current, self, stackless, 0);
    } else {
        /* pending throw.  Make the tasklet runnable */
        PyChannelObject *u_chan = NULL;
        PyTaskletObject *u_next;
        int u_dir;
        /* Unblock it if required*/
        if (self->flags.blocked) {
            /* we claim the channel's reference */
            slp_channel_remove_slow(self, &u_chan, &u_dir, &u_next);
        }
        fail = PyTasklet_Insert(self);
        if (u_chan) {
            if (fail)
                slp_channel_insert(u_chan, self, u_dir, u_next);
            else
                Py_DECREF(self);
        }
        ret = fail ? NULL : Py_None;
        Py_XINCREF(ret);
    }
    if (fail)
        TASKLET_SETVAL_OWN(self, tmpval);
    else
        Py_DECREF(tmpval);
    return ret;
}

static PyObject *
impl_tasklet_throw(PyTaskletObject *self, int pending, PyObject *exc, PyObject *val, PyObject *tb)
{
    STACKLESS_GETARG();
    PyThreadState *ts = _PyThreadState_GET();
    PyObject *ret, *bomb;

    if (ts->st.main == NULL)
        return PyTasklet_Throw_M(self, pending, exc, val, tb);

    bomb = slp_exc_to_bomb(exc, val, tb);
    if (bomb == NULL)
        return NULL;

    STACKLESS_PROMOTE_ALL();
#ifdef SLP_IMPL_THROW_BOMB_WITH_BOE
    ret = _impl_tasklet_throw_bomb(self, pending, bomb, 0);
#else
    ret = _impl_tasklet_throw_bomb(self, pending, bomb);
#endif
    STACKLESS_ASSERT();
    return ret;
}

int PyTasklet_Throw(PyTaskletObject *self, int pending, PyObject *exc,
                             PyObject *val, PyObject *tb)
{
    return slp_return_wrapper_hard(impl_tasklet_throw(self, pending, exc, val, tb));
}

static PyObject *
tasklet_throw(PyObject *myself, PyObject *args, PyObject *kwds)
{
    STACKLESS_GETARG();
    PyObject *result = NULL;
    PyObject *exc, *val=Py_None, *tb=Py_None;
    int pending;
    PyObject *pendingO = Py_False;
    char *kwlist[] = {"exc", "val", "tb", "pending", 0};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOO:throw", kwlist, &exc, &val, &tb, &pendingO))
        return NULL;
    pending = PyObject_IsTrue(pendingO);
    if (pending == -1)
        return NULL;
    STACKLESS_PROMOTE_ALL();
    result = impl_tasklet_throw(
        (PyTaskletObject*)myself, pending, exc, val, tb);
    STACKLESS_ASSERT();
    return result;
}


PyDoc_STRVAR(tasklet_raise_exception__doc__,
"tasklet.raise_exception(exc, value) -- raise an exception for the tasklet.\n\
exc must be a subclass of Exception.\n\
The tasklet is immediately activated.");

static PyObject *
tasklet_raise_exception(PyObject *myself, PyObject *args);

static PyObject *
PyTasklet_RaiseException_M(PyTaskletObject *self, PyObject *klass,
                           PyObject *args)
{
    PyMethodDef def = {"raise_exception", (PyCFunction)tasklet_raise_exception, METH_VARARGS};
    return PyStackless_CallCMethod_Main(&def, (PyObject *) self, "OO", klass, args);
}

static PyObject *
impl_tasklet_raise_exception(PyTaskletObject *self, PyObject *klass, PyObject *args)

{
    STACKLESS_GETARG();
    PyThreadState *ts = _PyThreadState_GET();
    PyObject *ret, *bomb;

    if (ts->st.main == NULL)
        return PyTasklet_RaiseException_M(self, klass, args);
    bomb = slp_make_bomb(klass, args, "tasklet.raise_exception");
    if (bomb == NULL)
        return NULL;

    STACKLESS_PROMOTE_ALL();
#ifdef SLP_IMPL_THROW_BOMB_WITH_BOE
    ret = _impl_tasklet_throw_bomb(self, 0, bomb, 1);
#else
    ret = _impl_tasklet_throw_bomb(self, 0, bomb);
#endif
    STACKLESS_ASSERT();

    return ret;
}

int PyTasklet_RaiseException(PyTaskletObject *self, PyObject *klass,
                             PyObject *args)
{
    return slp_return_wrapper_hard(impl_tasklet_raise_exception(self, klass, args));
}

static PyObject *
tasklet_raise_exception(PyObject *myself, PyObject *args)
{
    STACKLESS_GETARG();
    PyObject *result = NULL;
    PyObject *klass = PySequence_GetItem(args, 0);

    if (klass == NULL)
        TYPE_ERROR("raise_exception() takes at least 1 argument", NULL);
    args = PySequence_GetSlice(args, 1, PySequence_Size(args));
    if (!args) goto err_exit;
    STACKLESS_PROMOTE_ALL();
    result = impl_tasklet_raise_exception(
        (PyTaskletObject*)myself, klass, args);
    STACKLESS_ASSERT();
err_exit:
    Py_DECREF(klass);
    Py_XDECREF(args);
    return result;
}


PyDoc_STRVAR(tasklet_kill__doc__,
"tasklet.kill(pending=False) -- raise a TaskletExit exception for the tasklet.\n\
Note that this is a regular exception that can be caught.\n\
The tasklet is immediately activated.\n\
If the exception passes the toplevel frame of the tasklet,\n\
the tasklet will silently die.");

static PyObject *
impl_tasklet_kill(PyTaskletObject *task, int pending)
{
    STACKLESS_GETARG();
    PyObject *ret;

    /* We might be called without a thread state. If the tasklet
     * still has a frame, impl_tasklet_throw() will raise
     * RuntimeError. Therefore we need either to bind the tasklet to
     * a thread or drop its frames. Both makes sense, but the documentation
     * states, that Stackless does not silently change the thread of
     * a tasklet. Therefore we drop the frames.
     */
    assert(task->cstate);
    if (task->cstate->tstate == NULL) {
#ifdef SLP_TASKLET_KILL_REBINDS_THREAD
        /* No thread state. Silently bind the tasklet to
         * the current thread or drop its frames.
         * Either action prevents an error in impl_tasklet_throw().
         */
        if (task->cstate->nesting_level == 0 && task->f.frame) {
            /* rebind to the current thread */
            PyObject *arg = PyTuple_New(0);
            if (arg == NULL)
                return NULL;
            ret = tasklet_bind_thread((PyObject *)task, arg);
            Py_DECREF(arg);
            assert(ret != NULL); /* should not fail, if nesting_level is 0 */
            if (ret == NULL)  /* in case of a bug */
                return NULL;
            Py_DECREF(ret);
        } else {
            Py_CLEAR(task->f.frame);  /* or better tasklet_clear_frames(task) ? */
        }
#else
        /* drop the frame */
        Py_CLEAR(task->f.frame);  /* or better tasklet_clear_frames(task) ? */
#endif
    }

    /* we might be called after exceptions are gone */
    if (PyExc_TaskletExit == NULL) {
        PyExc_TaskletExit = PyUnicode_FromString("zombie");
        if (PyExc_TaskletExit == NULL)
            return NULL; /* give up */
    }
    STACKLESS_PROMOTE_ALL();
    ret = impl_tasklet_throw(task, pending, PyExc_TaskletExit, NULL, NULL);
    STACKLESS_ASSERT();
    return ret;
}

int PyTasklet_KillEx(PyTaskletObject *task, int pending)
{
    return slp_return_wrapper_hard(impl_tasklet_kill(task, pending));
}

int PyTasklet_Kill(PyTaskletObject *task)
{
    return slp_return_wrapper_hard(impl_tasklet_kill(task, 0));
}

static PyObject *
tasklet_kill(PyObject *self, PyObject *args, PyObject *kwds)
{
    STACKLESS_GETARG();
    int pending;
    PyObject *result;
    PyObject *pendingO = Py_False;
    char *kwlist[] = {"pending", 0};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:kill", kwlist, &pendingO))
        return NULL;
    pending = PyObject_IsTrue(pendingO);
    if (pending == -1)
        return NULL;
    STACKLESS_PROMOTE_ALL();
    result = impl_tasklet_kill((PyTaskletObject*)self, pending);
    STACKLESS_ASSERT();
    return result;
}


/* attributes which are hiding in small fields */

static PyObject *
tasklet_get_blocked(PyTaskletObject *task, void *closure)
{
    return PyBool_FromLong(task->flags.blocked);
}

int PyTasklet_GetBlocked(PyTaskletObject *task)
{
    return task->flags.blocked;
}


static PyObject *
tasklet_get_atomic(PyTaskletObject *task, void *closure)
{
    return PyBool_FromLong(task->flags.atomic);
}

int PyTasklet_GetAtomic(PyTaskletObject *task)
{
    return task->flags.atomic;
}


static PyObject *
tasklet_get_ignore_nesting(PyTaskletObject *task, void *closure)
{
    return PyBool_FromLong(task->flags.ignore_nesting);
}

int PyTasklet_GetIgnoreNesting(PyTaskletObject *task)
{
    return task->flags.ignore_nesting;
}


static PyObject *
tasklet_get_frame(PyTaskletObject *task, void *closure)
{
    PyObject *ret = (PyObject*) PyTasklet_GetFrame(task);
    if (ret)
        return ret;
    Py_RETURN_NONE;
}

PyObject *
PyTasklet_GetFrame(PyTaskletObject *task)
{
    PyFrameObject *f = (PyFrameObject *) slp_get_frame(task);

    while (f != NULL && !PyFrame_Check(f)) {
        f = f->f_back;
    }
    Py_XINCREF(f);
    return (PyObject *) f;
}


static PyObject *
tasklet_get_block_trap(PyTaskletObject *task, void *closure)
{
    return PyBool_FromLong(task->flags.block_trap);
}

int PyTasklet_GetBlockTrap(PyTaskletObject *task)
{
    return task->flags.block_trap;
}


static int
tasklet_set_block_trap(PyTaskletObject *task, PyObject *value, void *closure)
{
    if (!PyLong_Check(value))
        TYPE_ERROR("block_trap must be set to a bool or integer", -1);
    task->flags.block_trap = PyLong_AsLong(value) ? 1 : 0;
    return 0;
}

void PyTasklet_SetBlockTrap(PyTaskletObject *task, int value)
{
    task->flags.block_trap = value ? 1 : 0;
}


static PyObject *
tasklet_is_main(PyTaskletObject *task, void *closure)
{
    PyThreadState *ts = task->cstate->tstate;
    return PyBool_FromLong(ts && task == ts->st.main);
}

int
PyTasklet_IsMain(PyTaskletObject *task)
{
    PyThreadState *ts = task->cstate->tstate;
    return ts && task == ts->st.main;
}


static PyObject *
tasklet_is_current(PyTaskletObject *task, void *closure)
{
    PyThreadState *ts = task->cstate->tstate;
    return PyBool_FromLong(ts && task == ts->st.current);
}

int
PyTasklet_IsCurrent(PyTaskletObject *task)
{
    PyThreadState *ts = task->cstate->tstate;
    return ts && task == ts->st.current;
}


static PyObject *
tasklet_get_recursion_depth(PyTaskletObject *task, void *closure)
{
    PyThreadState *ts;

    assert(task->cstate != NULL);
    ts = task->cstate->tstate;
    return PyLong_FromLong((ts && ts->st.current == task) ? ts->recursion_depth
                                                  : task->recursion_depth);
}

int
PyTasklet_GetRecursionDepth(PyTaskletObject *task)
{
    PyThreadState *ts;

    assert(task->cstate != NULL);
    ts = task->cstate->tstate;
    return (ts && ts->st.current == task) ? ts->recursion_depth
                                  : task->recursion_depth;
}


static PyObject *
tasklet_get_nesting_level(PyTaskletObject *task, void *closure)
{
    PyThreadState *ts;

    assert(task->cstate != NULL);
    ts = task->cstate->tstate;
    return PyLong_FromLong(
        (ts && ts->st.current == task) ? ts->st.nesting_level
                               : task->cstate->nesting_level);
}

int
PyTasklet_GetNestingLevel(PyTaskletObject *task)
{
    PyThreadState *ts;

    assert(task->cstate != NULL);
    ts = task->cstate->tstate;
    return (ts && ts->st.current == task) ? ts->st.nesting_level
                                  : task->cstate->nesting_level;
}


/* attributes which are handy, but easily computed */

static PyObject *
tasklet_alive(PyTaskletObject *task, void *closure)
{
    return PyBool_FromLong(slp_get_frame(task) != NULL);
}

int
PyTasklet_Alive(PyTaskletObject *task)
{
    return slp_get_frame(task) != NULL;
}


static PyObject *
tasklet_paused(PyTaskletObject *task, void *closure)
{
    return PyBool_FromLong(
        slp_get_frame(task) != NULL && task->next == NULL);
}

int
PyTasklet_Paused(PyTaskletObject *task)
{
    return slp_get_frame(task) != NULL && task->next == NULL;
}


static PyObject *
tasklet_scheduled(PyTaskletObject *task, void *closure)
{
    return PyBool_FromLong(task->next != NULL);
}

int
PyTasklet_Scheduled(PyTaskletObject *task)
{
    return task->next != NULL;
}

static PyObject *
tasklet_restorable(PyTaskletObject *task, void *closure)
{
    PyThreadState *ts;

    assert(task->cstate != NULL);
    ts = task->cstate->tstate;
    return PyBool_FromLong(
        0 == ((ts && ts->st.current == task) ? ts->st.nesting_level
                                     : task->cstate->nesting_level) );
}

int
PyTasklet_Restorable(PyTaskletObject *task)
{
    PyThreadState *ts;

    assert(task->cstate != NULL);
    ts = task->cstate->tstate;
    return 0 == ((ts && ts->st.current == task) ? ts->st.nesting_level
                                        : task->cstate->nesting_level);
}

static PyObject *
tasklet_get_channel(PyTaskletObject *task, void *closure)
{
    PyTaskletObject *prev = task->prev;
    PyObject *ret = Py_None;
    if (prev != NULL && task->flags.blocked) {
        /* search left, optimizing in-oder access */
        while (!PyChannel_Check(prev))
            prev = prev->prev;
        ret = (PyObject *) prev;
    }
    Py_INCREF(ret);
    return ret;
}

static PyObject *
tasklet_get_next(PyTaskletObject *task, void *closure)
{
    PyObject *ret = Py_None;

    if (task->next != NULL && PyTasklet_Check(task->next))
        ret = (PyObject *) task->next;
    Py_INCREF(ret);
    return ret;
}

static PyObject *
tasklet_get_prev(PyTaskletObject *task, void *closure)
{
    PyObject *ret = Py_None;

    if (task->prev != NULL && PyTasklet_Check(task->prev))
        ret = (PyObject *) task->prev;
    Py_INCREF(ret);
    return ret;
}

static PyObject *
tasklet_thread_id(PyTaskletObject *task, void *closure)
{
    if (task->cstate->tstate) {
        return PyLong_FromUnsignedLong(task->cstate->tstate->thread_id);
    }
    return PyLong_FromLong(-1);
}

static PyObject *
tasklet_get_trace_function(PyTaskletObject *task, void *closure)
{
    PyThreadState *ts = task->cstate->tstate;
    PyObject *retval;

    retval = (ts && ts->st.current == task) ? ts->c_traceobj : task->traceobj;
    if (retval == NULL)
        retval = Py_None;
    Py_INCREF(retval);
    return retval;
}

static int
tasklet_set_trace_function(PyTaskletObject *task, PyObject *value, void *closure)
{
    PyThreadState *ts = task->cstate->tstate;
    Py_tracefunc tf = NULL;

    if (PySys_Audit("sys.settrace", NULL)) {
        return -1;
    }
    if (Py_None == value)
        value = NULL;
    if (value) {
        tf = slp_get_sys_trace_func();
        if (NULL == tf)
            return -1;
    }
    if (ts && ts->st.current == task) {
        /* current tasklet */
        if (_PyThreadState_GET() != ts)
            RUNTIME_ERROR("You cannot set the trace function of the current tasklet of another thread", -1);
        slp_set_trace(tf, value);
        return 0;
    }

    /* tasklet is not current */
    task->tracefunc = tf;
    Py_XINCREF(value);
    Py_XSETREF(task->traceobj, value);
    return 0;
}

static PyObject *
tasklet_get_profile_function(PyTaskletObject *task, void *closure)
{
    PyThreadState *ts = task->cstate->tstate;
    PyObject *retval;

    retval = (ts && ts->st.current == task) ? ts->c_profileobj : task->profileobj;
    if (retval == NULL)
        retval = Py_None;
    Py_INCREF(retval);
    return retval;
}

static int
tasklet_set_profile_function(PyTaskletObject *task, PyObject *value, void *closure)
{
    PyThreadState *ts = task->cstate->tstate;
    Py_tracefunc tf = NULL;

    if (PySys_Audit("sys.setprofile", NULL)) {
        return -1;
    }
    if (Py_None == value)
        value = NULL;
    if (value) {
        tf = slp_get_sys_profile_func();
        if (NULL == tf)
            return -1;
    }
    if (ts && ts->st.current == task) {
        /* current tasklet */
        if (_PyThreadState_GET() != ts)
            RUNTIME_ERROR("You cannot set the profile function of the current tasklet of another thread", -1);
        slp_set_profile(tf, value);
        return 0;
    }

    /* tasklet is not current */
    task->profilefunc = tf;
    Py_XINCREF(value);
    Py_XSETREF(task->profileobj, value);
    return 0;
}

/*[clinic input]
_stackless.tasklet.set_context

    context: object(subclass_of='&PyContext_Type')

Set the context to be used while this tasklet runs.

Every tasklet has a private context attribute. When the tasklet runs,
this context becomes the current context of the thread.

This method raises RuntimeError, if the tasklet is bound to a foreign thread and is current or scheduled.
This method raises RuntimeError, if called from within Context.run().
This method returns the tasklet it is called on.
[clinic start generated code]*/

static PyObject *
_stackless_tasklet_set_context_impl(PyTaskletObject *self, PyObject *context)
/*[clinic end generated code: output=23061bb958da0ff9 input=3c29aedc0d51481c]*/
{
    PyThreadState *ts = self->cstate->tstate;
    PyThreadState *cts = PyThreadState_Get();
    PyObject *ctx;

    assert(context);
    assert(PyContext_CheckExact(context));

    if (ts && self == ts->st.current) {
        /* the tasklet is the current tasklet. */

        /* I'm not sure, if setting the context for a current tasklet is really relevant,
         * but it can be implemented. Therefore I'm going to implement it. */
        if (ts != cts)
            goto fail_other_thread;

        /* Its context is in ts->context */
        ctx = ts->context;
        if (ctx && ((PyContext *) ctx)->ctx_entered)
            goto fail_ctx_entered;
        Py_INCREF(context);
        Py_XSETREF(ts->context, context);
        ts->context_ver++;
    } else {
        /* the tasklet is not the current tasklet. Its context is in self->context */
        if (ts != cts && PyTasklet_Scheduled(self) && !self->flags.blocked)
            goto fail_other_thread;
        ctx = self->context;
        if (ctx && ((PyContext *) ctx)->ctx_entered)
            goto fail_ctx_entered;
        Py_INCREF(context);
        Py_XSETREF(self->context, context);
    }
    Py_INCREF(self);
    return (PyObject *) self;
fail_ctx_entered:
    return slp_runtime_error("the current context of the tasklet has been entered.");
fail_other_thread:
    return slp_runtime_error("tasklet belongs to a different thread");
}

/* AFAIK argument clinic currently does not support the signature of context_run(callable, *args, **kwargs). */
PyDoc_STRVAR(tasklet_context_run__doc__,"context_run(callable, *args, **kwargs)\n\
\n\
Execute callable(*args, **kwargs) code in the context object of the tasklet the contest_run method is called on.\n\
Return the result of the execution or propagate an exception if one occurred.");

static PyObject *
tasklet_context_run(PyTaskletObject *self, PyObject *const *args,
            Py_ssize_t nargs, PyObject *kwnames)
{
    STACKLESS_GETARG();
    PyThreadState *ts = self->cstate->tstate;
    PyThreadState *cts = PyThreadState_Get();
    PyObject *ctx;
    assert(cts);

    if (nargs < 1) {
        PyErr_SetString(PyExc_TypeError,
                        "run() missing 1 required positional argument");
        return NULL;
    }

    ctx = _get_tasklet_context(self);  /* returns an new reference */

    PyObject * saved_context = cts->context;
    cts->context = ctx;
    cts->context_ver++;
    ctx = NULL;

    PyCFrameObject *f = NULL;
    if (stackless) {
        f = slp_cframe_new(slp_context_run_callback, 1);
        if (f == NULL) {
            Py_XSETREF(cts->context, saved_context);
            return NULL;
        }
        f->i = 1;
        Py_XINCREF(saved_context);
        f->ob1 = saved_context;
        SLP_SET_CURRENT_FRAME(ts, (PyFrameObject *)f);
        /* f contains the only counted reference to current frame. This reference
         * keeps the fame alive during the following _PyObject_FastCallKeywords().
         */
    }
    STACKLESS_PROMOTE_ALL();
    PyObject *call_result = _PyObject_Vectorcall(
        args[0], args + 1, nargs - 1, kwnames);
    STACKLESS_ASSERT();

    if (stackless && !STACKLESS_UNWINDING(call_result)) {
        /* required, because we added a C-frame */
        assert(f);
        assert((PyFrameObject *)f == SLP_CURRENT_FRAME(ts));
        SLP_STORE_NEXT_FRAME(ts, (PyFrameObject *)f);
        Py_DECREF(f);
        Py_XDECREF(saved_context);
        return STACKLESS_PACK(ts, call_result);
    }
    Py_XDECREF(f);
    if (STACKLESS_UNWINDING(call_result)) {
        Py_XDECREF(saved_context);
        return call_result;
    }
    Py_XSETREF(cts->context, saved_context);
    cts->context_ver++;
    return call_result;
}

static PyObject *
tasklet_context_id(PyTaskletObject *self, void *closure)
{
    PyObject *ctx = _get_tasklet_context(self);
    PyObject *result = PyLong_FromVoidPtr(ctx);
    Py_DECREF(ctx);
    return result;
}


static PyMemberDef tasklet_members[] = {
    {"cstate", T_OBJECT, offsetof(PyTaskletObject, cstate), READONLY,
     PyDoc_STR("the C stack object associated with the tasklet.\n\
     Every tasklet has a cstate, even if it is a trivial one.\n\
     Please see the cstate doc and the stackless documentation.")},
    {"tempval", T_OBJECT, offsetof(PyTaskletObject, tempval), 0},
    /* blocked, slicing_lock, atomic and such are treated by tp_getset */
    {0}
};

static PyGetSetDef tasklet_getsetlist[] = {
    {"next", (getter)tasklet_get_next, NULL,
     PyDoc_STR("the next tasklet in a a circular list of tasklets.")},

    {"prev", (getter)tasklet_get_prev, NULL,
     PyDoc_STR("the previous tasklet in a circular list of tasklets")},

    {"_channel", (getter)tasklet_get_channel, NULL,
     PyDoc_STR("The channel this tasklet is blocked on, or None if it is not blocked.\n"
     "This computed attribute may cause a linear search and should normally\n"
     "not be used, or be replaced by a real attribute in a derived type.")
    },

    {"blocked", (getter)tasklet_get_blocked, NULL,
     PyDoc_STR("Nonzero if waiting on a channel (1: send, -1: receive).\n"
     "Part of the flags word.")},

    {"atomic", (getter)tasklet_get_atomic, NULL,
     PyDoc_STR("atomic inhibits scheduling of this tasklet. See set_atomic()\n"
     "Part of the flags word.")},

    {"ignore_nesting", (getter)tasklet_get_ignore_nesting, NULL,
     PyDoc_STR("unless ignore_nesting is set, any nesting level > 0 inhibits\n"
     "auto-scheduling of this tasklet. See set_ignore_nesting()\n"
     "Part of the flags word.")},

    {"frame", (getter)tasklet_get_frame, NULL,
     PyDoc_STR("the current frame of this tasklet. For the running tasklet,\n"
     "this is redirected to tstate.frame.")},

    {"block_trap", (getter)tasklet_get_block_trap,
                   (setter)tasklet_set_block_trap,
     PyDoc_STR("An individual lock against blocking on a channel.\n"
     "This is used as a debugging aid to find out undesired blocking.\n"
     "Instead of trying to block, an exception is raised.")},

    {"is_main", (getter)tasklet_is_main, NULL,
     PyDoc_STR("There always exists exactly one tasklet per thread which acts as\n"
     "main. It receives all uncaught exceptions and can act as a watchdog.\n"
     "This attribute is computed.")},

    {"is_current", (getter)tasklet_is_current, NULL,
     PyDoc_STR("There always exists exactly one tasklet per thread which is "
     "currently running.\n"
     "This attribute is computed.")},

    {"paused", (getter)tasklet_paused, NULL,
     PyDoc_STR("A tasklet is said to be paused if it is neither in the runnables list\n"
     "nor blocked, but alive. This state is entered after a t.remove()\n"
     "or by the main tasklet, when it is acting as a watchdog.\n"
     "This attribute is computed.")},

    {"scheduled", (getter)tasklet_scheduled, NULL,
     PyDoc_STR("A tasklet is said to be scheduled if it is either in the runnables list\n"
     "or waiting in a channel.\n"
     "This attribute is computed.")},

    {"recursion_depth", (getter)tasklet_get_recursion_depth, NULL,
     PyDoc_STR("The system recursion_depth is replicated for every tasklet.\n"
     "They all start running with a recursion_depth of zero.")},

    {"nesting_level", (getter)tasklet_get_nesting_level, NULL,
     PyDoc_STR("The interpreter nesting level is monitored by every tasklet.\n"
     "They all start running with a nesting level of zero.")},

    {"restorable", (getter)tasklet_restorable, NULL,
     PyDoc_STR("True, if the tasklet can be completely restored by pickling/unpickling.\n"
     "All tasklets can be pickled for debugging/inspection purposes, but an \n"
     "unpickled tasklet might have lost runtime information (C stack).")},

    {"alive", (getter)tasklet_alive, NULL,
     PyDoc_STR("A tasklet is alive if it has an associated frame.\n"
     "This attribute is computed.")},

    {"thread_id", (getter)tasklet_thread_id, NULL,
     PyDoc_STR("Return the thread id of the thread the tasklet belongs to.")},

    {"trace_function", (getter)tasklet_get_trace_function,
                   (setter)tasklet_set_trace_function,
     PyDoc_STR("The trace function of this tasklet. None by default.\n"
     "For the current tasklet this property is equivalent to sys.gettrace()\n"
     "and sys.settrace().")},

    {"profile_function", (getter)tasklet_get_profile_function,
                   (setter)tasklet_set_profile_function,
     PyDoc_STR("The trace function of this tasklet. None by default.\n"
     "For the current tasklet this property is equivalent to sys.gettrace()\n"
     "and sys.settrace().")},

     {"context_id", (getter)tasklet_context_id, NULL,
      PyDoc_STR("The id of the context object of this tasklet.")},

    {0},
};

#define PCF PyCFunction
#define METH_VS METH_VARARGS | METH_STACKLESS
#define METH_KS METH_VARARGS | METH_KEYWORDS | METH_STACKLESS
#define METH_NS METH_NOARGS | METH_STACKLESS

static PyMethodDef tasklet_methods[] = {
    {"insert",                  (PCF)tasklet_insert,        METH_NOARGS,
      tasklet_insert__doc__},
    {"run",                     (PCF)tasklet_run,           METH_NS,
     tasklet_run__doc__},
    {"switch",                  (PCF)tasklet_switch,        METH_NS,
     tasklet_switch__doc__},
    {"remove",                  (PCF)tasklet_remove,        METH_NOARGS,
     tasklet_remove__doc__},
    {"set_atomic",              (PCF)tasklet_set_atomic,    METH_O,
     tasklet_set_atomic__doc__},
    {"set_ignore_nesting", (PCF)tasklet_set_ignore_nesting, METH_O,
     tasklet_set_ignore_nesting__doc__},
    {"throw",            (PCF)(void(*)(void))tasklet_throw, METH_KS,
    tasklet_throw__doc__},
    {"raise_exception",       (PCF)tasklet_raise_exception, METH_VS,
    tasklet_raise_exception__doc__},
    {"kill",              (PCF)(void(*)(void))tasklet_kill, METH_KS,
     tasklet_kill__doc__},
    {"bind",              (PCF)(void(*)(void))tasklet_bind, METH_VARARGS | METH_KEYWORDS,
     tasklet_bind__doc__},
    {"setup",            (PCF)(void(*)(void))tasklet_setup, METH_VARARGS | METH_KEYWORDS,
     tasklet_setup__doc__},
    {"__reduce__",              (PCF)tasklet_reduce,        METH_NOARGS,
     tasklet_reduce__doc__},
    {"__reduce_ex__",           (PCF)tasklet_reduce,        METH_O,
     tasklet_reduce__doc__},
    {"__setstate__",            (PCF)tasklet_setstate,      METH_O,
     tasklet_setstate__doc__},
    {"bind_thread",              (PCF)tasklet_bind_thread,  METH_VARARGS,
    tasklet_bind_thread__doc__},
    {"context_run", (PCF)(void(*)(void))tasklet_context_run, METH_FASTCALL | METH_KEYWORDS | METH_STACKLESS,
            tasklet_context_run__doc__},
    _STACKLESS_TASKLET_SET_CONTEXT_METHODDEF
    {NULL,     NULL}             /* sentinel */
};

PyDoc_STRVAR(tasklet__doc__,
"A tasklet object represents a tiny task in a Python(r) thread.\n\
At program start, there is always one running main tasklet.\n\
New tasklets can be created with methods from the stackless\n\
module.\n\
");


PyTypeObject PyTasklet_Type = {
    PyVarObject_HEAD_INIT(&PyType_Type, 0)
    .tp_name = "_stackless.tasklet",
    .tp_basicsize = sizeof(PyTaskletObject),
    .tp_dealloc = (destructor)tasklet_dealloc,
    .tp_call = tasklet_setup,
    .tp_getattro = PyObject_GenericGetAttr,
    .tp_setattro = PyObject_GenericSetAttr,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC |
        Py_TPFLAGS_BASETYPE,
    .tp_doc = tasklet__doc__,
    .tp_traverse = (traverseproc)tasklet_traverse,
    .tp_clear = (inquiry) tasklet_clear,
    .tp_weaklistoffset = offsetof(PyTaskletObject, tsk_weakreflist),
    .tp_methods = tasklet_methods,
    .tp_members = tasklet_members,
    .tp_getset = tasklet_getsetlist,
    .tp_init = tasklet_init,
    .tp_new = tasklet_new,
    .tp_free = PyObject_GC_Del,
    .tp_finalize = tasklet_finalize
};
#endif
