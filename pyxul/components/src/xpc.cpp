/*
# Python for XUL
# copyright © 2021 Malek Hadj-Ali
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 3
# as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


#include "xpc.h"

#include "errors.h"
#include "modules.h"
#include "python.h"

#include "wrappers/api.h"

#include "nsIChromeRegistry.h"
#include "nsIConsoleService.h"
#include "nsIXPConnect.h"
#include "xpcIJSModuleLoader.h"

#include "nsDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsNetUtil.h"
#include "nsXULAppAPI.h"

#include "mozilla/dom/Exceptions.h"


using namespace pyxul::wrappers;


namespace pyxul {


/* AutoReporter ------------------------------------------------------------- */

AutoReporter::AutoReporter(JSContext *aCx): mCx(aCx)
{
    mWarningReporter = JS::SetWarningReporter(mCx, ReportWarning);
}


AutoReporter::~AutoReporter()
{
    if (JS_IsExceptionPending(mCx)) {
        JS::RootedValue aException(mCx, JS::UndefinedValue());
        if (JS_GetPendingException(mCx, &aException) && aException.isObject()) {
            JS_ClearPendingException(mCx);
            JS::RootedObject aError(mCx, &aException.toObject());
            if (!aError || !ReportError(aError)) {
                JS_SetPendingException(mCx, aException);
            }
        }
    }
    JS::SetWarningReporter(mCx, mWarningReporter);
}


bool
AutoReporter::ReportError(JS::HandleObject aError)
{
    bool result = false;
    JS::RootedValue aJSValue(mCx);
    PyObject *name = nullptr, *message = nullptr;

    if (JS_GetProperty(mCx, aError, "name", &aJSValue)) {
        if (!(name = pyjs::Wrap(mCx, aJSValue))) {
            PyErr_Clear();
        }
    }
    if (JS_GetProperty(mCx, aError, "message", &aJSValue)) {
        if (!(message = pyjs::Wrap(mCx, aJSValue))) {
            PyErr_Clear();
        }
    }
    if (name && message) {
        PyErr_Format(errors::JSError, "%S: %S", name, message);
        result = true;
    }
    Py_CLEAR(message);
    Py_CLEAR(name);
    return result;
}


void
AutoReporter::ReportWarning(JSContext *aCx, JSErrorReport *aReport)
{
    if (JSREPORT_IS_WARNING(aReport->flags)) {
        if (aReport->filename) {
            PyErr_WarnExplicit(
                errors::JSWarning, aReport->message().c_str(),
                aReport->filename, aReport->lineno, nullptr, nullptr
            );
        }
        else {
            PyErr_WarnEx(errors::JSWarning, aReport->message().c_str(), 1);
        }
    }
    else {
        PyErr_SetString(errors::JSError, aReport->message().c_str());
    }
}


namespace xpc {


/* utils -------------------------------------------------------------------- */

JSObject *
Unwrap(JSObject *aObject)
{
    JSObject *aResult = nullptr;

    if ((aResult = js::UncheckedUnwrap(aObject, false))) {
        JS::ExposeObjectToActiveJS(aResult);
    }
    return aResult;
}


bool
WrapJS(JSContext *aCx, JSObject *aObject, const nsIID &aIID, void **aResult)
{
    nsCOMPtr<nsIXPConnect> aXpc;

    PY_ENSURE_TRUE(
        (aXpc = GetXPConnect()), false, errors::XPCOMError,
        "Failed to get XPConnect"
    );
    return (NS_FAILED(aXpc->WrapJS(aCx, aObject, aIID, aResult))) ? false : true;
}


namespace { // anonymous


static JSObject *
__global__(void)
{
    nsIGlobalObject *aGlobal = nullptr;

    if (
        !(aGlobal = dom::GetCurrentGlobal()) &&
        !(aGlobal = dom::GetIncumbentGlobal()) &&
        !(aGlobal = dom::GetEntryGlobal())
    ) {
        PyErr_SetString(errors::XPCOMError, "Failed to get global object");
        return nullptr;
    }
    return aGlobal->GetGlobalJSObject();
}


} // namespace anonymous


PyObject *
GetGlobalJSObject(void)
{
    AutoJSContext aCx;
    AutoReporter ar(aCx);

    JS::RootedObject aGlobal(aCx, __global__());
    if (aGlobal) {
        return pyjs::WrapObject(aCx, &aGlobal);
    }
    return nullptr;
}


namespace { // anonymous


static nsresult
__import__(JSContext *aCx, const nsACString &aUri, JS::HandleValue aTarget)
{
    nsCOMPtr<xpcIJSModuleLoader> aLoader;
    nsresult rv = NS_ERROR_FAILURE;

    aLoader = do_GetService("@mozilla.org/moz/jsloader;1", &rv);
    PY_ENSURE_SUCCESS(
        rv, rv, errors::XPCOMError, "Failed to get JSModuleLoader"
    );
    JS::RootedValue aResult(aCx);
    return aLoader->Import(aUri, aTarget, aCx, 1, &aResult);
}


} // namespace anonymous


bool
ImportModule(const char *aUri, PyObject *aTarget)
{
    nsCString uri;

    AutoJSContext aCx;
    AutoReporter ar(aCx);

    uri.Assign(aUri);
    JS::RootedValue target(aCx, jspy::Wrap(aCx, aTarget));
    if (target.isUndefined()) {
        return false;
    }
    PY_ENSURE_SUCCESS(
        __import__(aCx, uri, target),
        false, errors::XPCOMError, "Failed to import module from: '%s'", aUri
    );
    return true;
}


/* errors/warnings handling ------------------------------------------------- */

bool
ReportError()
{
    if (!PyErr_Occurred()) {
        PyErr_SetString(
            PyExc_SystemError, "error reported without an exception set"
        );
    }
    PyErr_Print();
    return false;
}


bool
SetException(PyObject *aType, PyObject *aValue, PyObject *aTraceback)
{
    /*
    // TEMP
    printf("\n\n\nERROR ============================================================================================\n");
    PyErr_Display(aType, aValue, aTraceback);
    printf("ERROR ============================================================================================\n\n\n\n");
    // TEMP
    */

    nsCOMPtr<nsIStackFrame> aLocation;
    nsCOMPtr<nsIException> aMaybeCause, aException;
    nsCOMPtr<nsIXPConnect> aXpc;

    aLocation = dom::GetCurrentJSStack();

    AutoJSContext aCx;

    if (JS_IsExceptionPending(aCx)) {
        JS::RootedValue aCurrent(aCx, JS::UndefinedValue());
        if (JS_GetPendingException(aCx, &aCurrent) && aCurrent.isObject()) {
            JS_ClearPendingException(aCx);
            JS::RootedObject aError(aCx, &aCurrent.toObject());
            if (
                !aError ||
                !WrapJS(
                    aCx, aError, NS_GET_IID(nsIException),
                    getter_AddRefs(aMaybeCause)
                )
            ) {
                JS_SetPendingException(aCx, aCurrent);
            }
        }
    }

    if (!(aException = errors::Exception(aCx, aValue, aLocation, aMaybeCause))) {
        return false;
    }
    dom::ThrowExceptionObject(aCx, aException);
    return true;
}


bool
ReportWarning(
    PyObject *aMessage, PyObject *aCategory, PyObject *aFilename,
    PyObject *aLineno, PyObject *aLine
)
{
    /*
    // TEMP
    printf("\n\n\nWARNING ============================================================================================\n");
    _Py_IDENTIFIER(__showwarning__);
    PyObject *warnings = PyImport_AddModule("warnings"); // borrowed
    Py_XDECREF(
        _PyObject_CallMethodIdObjArgs(
            warnings, &PyId___showwarning__, aMessage, aCategory, aFilename,
            aLineno, nullptr
        )
    );
    printf("WARNING ============================================================================================\n\n\n\n");
    // TEMP
    */

    nsresult rv = NS_ERROR_FAILURE;
    nsCOMPtr<nsIScriptError> aWarning;
    nsCOMPtr<nsIConsoleService> aConsoleService;

    aWarning = errors::Warning(aMessage, aCategory, aFilename, aLineno, aLine);
    if (!aWarning) {
        return false;
    }
    aConsoleService = do_GetService(NS_CONSOLESERVICE_CONTRACTID, &rv);
    PY_ENSURE_SUCCESS(
        rv, false, errors::XPCOMError, "Failed to get ConsoleService"
    );
    rv = aConsoleService->LogMessage(aWarning);
    PY_ENSURE_SUCCESS(rv, false, errors::XPCOMError, "Failed to log warning");
    return true;
}


void
ThrowResult(nsresult rv)
{
    AutoJSContext aCx;
    AutoReporter ar(aCx);
    dom::ThrowResult(aCx, rv);
}


/* --------------------------------------------------------------------------
   pyRuntime interface
   -------------------------------------------------------------------------- */


namespace { // anonymous


static void
__gc__(JSContext *aCx)
{
    JS::PrepareForFullGC(aCx);
    JS::GCForReason(aCx, GC_NORMAL, JS::gcreason::API);
    js::gc::FinishGC(aCx);
}


static void
__collect__()
{
    AutoJSContext aCx;

    _Py_Collect();
    __gc__(aCx);
    _Py_Collect();
}


} // namespace anonymous


/* Execute ------------------------------------------------------------------ */

namespace { // anonymous


static bool
__file__(
    const nsACString &aSrc, const char *aOriginCharset, nsIURI *aBaseURI,
    nsIFile **aFile
)
{
    nsresult rv = NS_ERROR_FAILURE;
    nsCOMPtr<nsIChromeRegistry> aChromeRegistry;
    nsCOMPtr<nsIURI> aChromeURI, aFileURI;
    nsCOMPtr<nsIFileURL> aFileURL;
    nsCString aScheme;
    bool isChrome = false, gotScheme;

    rv = NS_NewURI(getter_AddRefs(aChromeURI), aSrc, aOriginCharset, aBaseURI);
    PY_ENSURE_SUCCESS(
        rv, false, errors::XPCOMError, "Failed to create URI from 'src'"
    );

    rv = aChromeURI->SchemeIs("chrome", &isChrome);
    PY_ENSURE_SUCCESS(
        rv, false, errors::XPCOMError, "Failed to check URI's scheme"
    );
    if (!isChrome) {
        rv = aChromeURI->GetScheme(aScheme);
        gotScheme = NS_SUCCEEDED(rv);
        PyErr_Format(
            errors::Error, "python 'src' attribute's scheme must be chrome%s%s",
            gotScheme ? ", not " : "", gotScheme ? aScheme.get() : ""
        );
        return false;
    }

    aChromeRegistry = GetChromeRegistryService();
    PY_ENSURE_TRUE(
        aChromeRegistry, false, errors::XPCOMError,
        "Failed to get nsIChromeRegistry"
    );

    rv = aChromeRegistry->ConvertChromeURL(aChromeURI, getter_AddRefs(aFileURI));
    PY_ENSURE_SUCCESS(
        rv, false, errors::XPCOMError, "Failed to convert chrome URI"
    );

    aFileURL = do_QueryInterface(aFileURI, &rv);
    PY_ENSURE_SUCCESS(
        rv, false, errors::XPCOMError, "Failed to QueryInterface to nsIFileURL"
    );

    rv = aFileURL->GetFile(aFile);
    PY_ENSURE_SUCCESS(rv, false, errors::XPCOMError, "Failed to get nsIFile");
    return true;
}


static bool
__execute__(nsIFile *aFile, PyObject *globals)
{
    nsCString aPath;
    FILE *fp;
    PyObject *path = nullptr;
    bool result = false;

    if (NS_FAILED(aFile->GetNativePath(aPath))) {
        PyErr_SetString(errors::XPCOMError, "Failed to get native path");
    }
    else if (NS_FAILED(aFile->OpenANSIFileDesc("r", &fp))) {
        PyErr_SetFromErrnoWithFilename(PyExc_OSError, aPath.get());
    }
    else {
        if ((path = PyUnicode_DecodeFSDefault(aPath.get()))) { // +1
            result = _PyExecute_File(fp, path, globals) ? false : true;
            Py_DECREF(path); // -1
        }
        fclose(fp);
    }
    return result;
}


static void
__update__(PyObject *window, PyObject *globals, PyObject *names)
{
    Py_ssize_t size = PyList_GET_SIZE(names), i;
    PyObject *name = nullptr, *attr = nullptr;

    for (i = 0; i < size; i++) {
        name = PyList_GET_ITEM(names, i); // borrowed
        if (!PyUnicode_Check(name)) {
            PyErr_SetString(PyExc_TypeError, "__js__'s items must be strings");
            break;
        }
        attr = PyDict_GetItemWithError(globals, name); // borrowed
        if (!attr) {
            if (!PyErr_Occurred()) {
                PyErr_Format(
                    PyExc_NameError, "global name '%U' is not defined", name
                );
            }
            break;
        }
        if (PyObject_SetAttr(window, name, attr)) {
            break;
        }
    }
}


static bool
__finish__(PyObject *window, PyObject *globals)
{
    _Py_IDENTIFIER(__js__);
    PyObject *names = nullptr;

    if ((names = _PyDict_GetItemId(globals, &PyId___js__))) { // borrowed
        if (!PyList_CheckExact(names)) {
            PyErr_SetString(PyExc_TypeError, "__js__ must be a list");
        }
        else {
            __update__(window, globals, names);
        }
        _PyDict_DelItemId(globals, &PyId___js__);
    }
    return !PyErr_Occurred();
}


} // namespace anonymous


nsresult
Execute(
    nsIDOMWindow *aDOMWindow,
    const nsACString &aSrc, const char *aOriginCharset, nsIURI *aBaseURI
)
{
    AutoGILState ags; // XXX: important

    _Py_IDENTIFIER(window);
    PyObject *window = nullptr, *main = nullptr, *globals = nullptr;
    nsCOMPtr<nsIFile> aFile;
    nsresult rv = NS_ERROR_FAILURE;

    nsCOMPtr<nsIGlobalObject> aGlobal = do_QueryInterface(aDOMWindow);
    if (aGlobal) {
        dom::AutoEntryScript aes(aGlobal, "pyxul::xpc::Execute");
        JSContext *aCx = aes.cx();
        AutoReporter ar(aCx);
        JS::RootedObject aWindow(aCx, aGlobal->GetGlobalJSObject());
        if (aWindow && (window = pyjs::WrapObject(aCx, &aWindow))) { // +1
            if ((main = PyImport_AddModule("__main__"))) { // borrowed
                Py_INCREF(main); // +1
                if ((globals = PyModule_GetDict(main))) { // borrowed
                    Py_INCREF(globals); // +1
                    if (
                        !_PyDict_SetItemId(globals, &PyId_window, window) &&
                        __file__(
                            aSrc, aOriginCharset, aBaseURI, getter_AddRefs(aFile)
                        ) &&
                        __execute__(aFile, globals) &&
                        __finish__(window, globals)
                    ) {
                        rv = NS_OK;
                    }
                    Py_DECREF(globals); // -1
                }
                Py_DECREF(main); // -1
            }
            Py_DECREF(window); // -1
        }
        else {
            PyErr_SetString(
                errors::XPCOMError,
                "Failed to get global JSObject from nsIGlobalObject"
            );
        }
    }
    else {
        PyErr_SetString(
            errors::XPCOMError,
            "Failed to get nsIGlobalObject from nsIDOMWindow"
        );
    }
    if (PyErr_Occurred()) {
        ReportError();
    }
    return rv;
}


/* Cleanup ------------------------------------------------------------------ */

namespace { // anonymous


typedef nsTHashtable<nsPtrHashKey<JSObject>> JSObjectSet;


static void
__tidyup__(JSContext *aCx, JS::HandleObject aJSObject, JSObjectSet &aSeen)
{
    aSeen.PutEntry(aJSObject);

    JSAutoCompartment ac(aCx, aJSObject);

    JS::Rooted<JS::IdVector> aIds(aCx, JS::IdVector(aCx));
    JS::RootedId aId(aCx);
    JS::Rooted<JS::PropertyDescriptor> aDesc(aCx);
    JS::RootedObject aProperty(aCx);

    if (!JS_Enumerate(aCx, aJSObject, &aIds)) {
        return;
    }
    size_t aLength = aIds.length();
    for (size_t i = 0; i < aLength; i++) {
        aId = aIds[i];
        if (!JS_GetPropertyDescriptorById(aCx, aJSObject, aId, &aDesc)) {
            return;
        }
        if (aDesc.object() && aDesc.value().isObject()) {
            aProperty = &aDesc.value().toObject();
            if (jspy::Check(&aProperty)) {

                /*{
                    JSAutoCompartment aPc(aCx, aProperty);
                    printf(
                        "\t\twrappers: %s\n",
                        js::ObjectClassName(aCx, aProperty)
                    );
                }*/

                if (
                    !JS_SetPropertyById(
                        aCx, aJSObject, aId, JS::UndefinedHandleValue
                    )
                ) {
                    return;
                }
            }
            else if (!aSeen.Contains(aProperty)) {
                __tidyup__(aCx, aProperty, aSeen);
            }
        }
    }
}


static void
__cleanup__(JSContext *aCx, JS::HandleObject aJSObject)
{
    JSObjectSet aSeen(32);

    __tidyup__(aCx, aJSObject, aSeen);
    aSeen.Clear();
    __gc__(aCx);
}


} // namespace anonymous


void
Cleanup(nsIDOMWindow *aDOMWindow)
{
    AutoGILState ags; // XXX: important

    nsCOMPtr<nsIGlobalObject> aGlobal = do_QueryInterface(aDOMWindow);
    if (aGlobal) {
        dom::AutoEntryScript aes(aGlobal, "pyxul::xpc::Cleanup");
        JSContext *aCx = aes.cx();
        AutoReporter ar(aCx);
        JS::RootedObject aWindow(aCx, Unwrap(aGlobal->GetGlobalJSObject()));
        if (aWindow) {
            __cleanup__(aCx, aWindow);
        }
        else {
            PyErr_SetString(
                errors::XPCOMError,
                "Failed to get global JSObject from nsIGlobalObject"
            );
        }
    }
    else {
        PyErr_SetString(
            errors::XPCOMError,
            "Failed to get nsIGlobalObject from nsIDOMWindow"
        );
    }
    if (PyErr_Occurred()) {
        ReportError();
    }
}


/* LoadModule --------------------------------------------------------------- */

const mozilla::Module *
LoadModule(mozilla::FileLocation &aFileLocation)
{
    AutoGILState ags; // XXX: important

    nsCOMPtr<nsIFile> aFile;
    nsCString aPath;
    const mozilla::Module *aModule = nullptr;

    if (aFileLocation.IsZip()) {
        PyErr_SetString(PyExc_ImportError, "Cannot import module from zip");
    }
    else if (!(aFile = aFileLocation.GetBaseFile())) {
        PyErr_SetString(errors::XPCOMError, "Failed to get file from location");
    }
    else if (NS_FAILED(aFile->GetNativePath(aPath))) {
        PyErr_SetString(errors::XPCOMError, "Failed to get native path from file");
    }
    else {
        aModule = modules::Module::Load(aPath, aFile);
    }
    if (!aModule) {
        ReportError();
    }
    return aModule;
}


/* Initialize/Finalize ------------------------------------------------------ */

namespace { // anonymous


static bool
__getSpecialDir__(
    const char *aSpecialDirName, const char **dirs, nsACString &aPath
)
{
    nsCOMPtr<nsIFile> aSpecialDir;
    const char *dir = nullptr;

    PY_ENSURE_SUCCESS(
        NS_GetSpecialDirectory(aSpecialDirName, getter_AddRefs(aSpecialDir)),
        false, errors::XPCOMError,
        "Failed to get special dir '%s'", aSpecialDirName
    );
    while ((dir = *dirs)) {
        PY_ENSURE_SUCCESS(
            aSpecialDir->AppendNative(nsLiteralCString(dir)),
            false, errors::XPCOMError,
            "Failed to append '%s' to special dir '%s'", dir, aSpecialDirName
        );
        dirs ++;
    }
    PY_ENSURE_SUCCESS(
        aSpecialDir->GetNativePath(aPath),
        false, errors::XPCOMError, "Failed to get updated path"
    );
    return true;
}


static bool
__initialize__()
{
    AutoGILState ags; // XXX: important

    static const char *distDirs[] = {
        "extensions", PYXUL_ID, "components", "dist-packages", nullptr
    };
    static const char *siteDirs[] = {"site-packages", nullptr};
    nsCString aDistPath, aSitePath;

    if (
        errors::Initialize() &&
        __getSpecialDir__(XRE_APP_DISTRIBUTION_DIR, distDirs, aDistPath) &&
        __getSpecialDir__(NS_OS_CURRENT_PROCESS_DIR, siteDirs, aSitePath) &&
        !_Py_Setup(aDistPath.get(), aSitePath.get()) &&
        pyjs::Initialize() &&
        jspy::Initialize()
    ) {
        return true;
    }
    return ReportError();
}


static void
__finalize__()
{
    AutoGILState ags; // XXX: important

    modules::Finalize();
    jspy::Finalize();
    errors::Finalize();

    __collect__();
}


} // namespace anonymous


bool
Initialize()
{
    if (_Py_Initialize()) {
        return __initialize__();
    }
    return false;
}


void
Finalize()
{
    if (Py_IsInitialized()) {
        __finalize__();
        _Py_Finalize();
    }
}


} // namespace xpc


} // namespace pyxul

