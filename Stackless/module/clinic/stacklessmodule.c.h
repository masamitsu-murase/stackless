/*[clinic input]
preserve
[clinic start generated code]*/

#if defined(STACKLESS)

PyDoc_STRVAR(_stackless_pickle_flags_default__doc__,
"pickle_flags_default($module, /, new_default=-1, mask=-1)\n"
"--\n"
"\n"
"Get and set the per interpreter default value for pickle-flags.\n"
"\n"
"  new_default\n"
"    The new default value for pickle-flags.\n"
"  mask\n"
"    A bit mask, that indicates the valid bits in argument \"new_default\".\n"
"\n"
"The function returns the previous pickle-flags. To inquire the pickle-flags\n"
"without changing them, omit the arguments.");

#define _STACKLESS_PICKLE_FLAGS_DEFAULT_METHODDEF    \
    {"pickle_flags_default", (PyCFunction)(void(*)(void))_stackless_pickle_flags_default, METH_FASTCALL|METH_KEYWORDS, _stackless_pickle_flags_default__doc__},

static PyObject *
_stackless_pickle_flags_default_impl(PyObject *module, long new_default,
                                     long mask);

static PyObject *
_stackless_pickle_flags_default(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"new_default", "mask", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "pickle_flags_default", 0};
    PyObject *argsbuf[2];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    long new_default = -1;
    long mask = -1;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 2, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    if (args[0]) {
        if (PyFloat_Check(args[0])) {
            PyErr_SetString(PyExc_TypeError,
                            "integer argument expected, got float" );
            goto exit;
        }
        new_default = PyLong_AsLong(args[0]);
        if (new_default == -1 && PyErr_Occurred()) {
            goto exit;
        }
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    if (PyFloat_Check(args[1])) {
        PyErr_SetString(PyExc_TypeError,
                        "integer argument expected, got float" );
        goto exit;
    }
    mask = PyLong_AsLong(args[1]);
    if (mask == -1 && PyErr_Occurred()) {
        goto exit;
    }
skip_optional_pos:
    return_value = _stackless_pickle_flags_default_impl(module, new_default, mask);

exit:
    return return_value;
}

#endif /* defined(STACKLESS) */

#if defined(STACKLESS)

PyDoc_STRVAR(_stackless_pickle_flags__doc__,
"pickle_flags($module, /, new_flags=-1, mask=-1)\n"
"--\n"
"\n"
"Get and set pickle-flags.\n"
"\n"
"  new_flags\n"
"    The new value for pickle-flags of the current thread.\n"
"  mask\n"
"    A bit mask, that indicates the valid bits in argument \"new_flags\".\n"
"\n"
"A number of option flags control various aspects of Stackless pickling\n"
"behavior. The flags are represented by the bits of an integer value.\n"
"The function returns the previous pickle-flags. To inquire the\n"
"pickle-flags without changing them, omit the arguments.\n"
"\n"
"Currently the following bits are defined:\n"
" - bit 0, value 1: pickle the tracing/profiling state of a tasklet;\n"
" - bit 1, value 2: preserve the finalizer of an asynchronous generator;\n"
" - bit 2, value 4: reset the finalizer of an asynchronous generator.\n"
"All other bits must be set to 0.");

#define _STACKLESS_PICKLE_FLAGS_METHODDEF    \
    {"pickle_flags", (PyCFunction)(void(*)(void))_stackless_pickle_flags, METH_FASTCALL|METH_KEYWORDS, _stackless_pickle_flags__doc__},

static PyObject *
_stackless_pickle_flags_impl(PyObject *module, long new_flags, long mask);

static PyObject *
_stackless_pickle_flags(PyObject *module, PyObject *const *args, Py_ssize_t nargs, PyObject *kwnames)
{
    PyObject *return_value = NULL;
    static const char * const _keywords[] = {"new_flags", "mask", NULL};
    static _PyArg_Parser _parser = {NULL, _keywords, "pickle_flags", 0};
    PyObject *argsbuf[2];
    Py_ssize_t noptargs = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0) - 0;
    long new_flags = -1;
    long mask = -1;

    args = _PyArg_UnpackKeywords(args, nargs, NULL, kwnames, &_parser, 0, 2, 0, argsbuf);
    if (!args) {
        goto exit;
    }
    if (!noptargs) {
        goto skip_optional_pos;
    }
    if (args[0]) {
        if (PyFloat_Check(args[0])) {
            PyErr_SetString(PyExc_TypeError,
                            "integer argument expected, got float" );
            goto exit;
        }
        new_flags = PyLong_AsLong(args[0]);
        if (new_flags == -1 && PyErr_Occurred()) {
            goto exit;
        }
        if (!--noptargs) {
            goto skip_optional_pos;
        }
    }
    if (PyFloat_Check(args[1])) {
        PyErr_SetString(PyExc_TypeError,
                        "integer argument expected, got float" );
        goto exit;
    }
    mask = PyLong_AsLong(args[1]);
    if (mask == -1 && PyErr_Occurred()) {
        goto exit;
    }
skip_optional_pos:
    return_value = _stackless_pickle_flags_impl(module, new_flags, mask);

exit:
    return return_value;
}

#endif /* defined(STACKLESS) */

#ifndef _STACKLESS_PICKLE_FLAGS_DEFAULT_METHODDEF
    #define _STACKLESS_PICKLE_FLAGS_DEFAULT_METHODDEF
#endif /* !defined(_STACKLESS_PICKLE_FLAGS_DEFAULT_METHODDEF) */

#ifndef _STACKLESS_PICKLE_FLAGS_METHODDEF
    #define _STACKLESS_PICKLE_FLAGS_METHODDEF
#endif /* !defined(_STACKLESS_PICKLE_FLAGS_METHODDEF) */
/*[clinic end generated code: output=25a1592662352a4a input=a9049054013a1b77]*/
