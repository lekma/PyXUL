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


#include "errors.h"

#include "python.h"

#include "nsIProgrammingLanguage.h"

#include "prprf.h"


namespace pyxul::errors {


namespace { // anonymous


static const char messageFmt[] = ": %s";
static const char filenameFmt[] = "  File \"%s\"";
static const char lineNumberFmt[] = ", line %ld";
static const char sourceLineFmt[] = "\n    %s";


static void
nsCString_AppendPrintf(nsACString &self, const char *aFormat, ...)
{
    char *aData = nullptr;

    va_list ap;
    va_start(ap, aFormat);
    aData = PR_vsmprintf(aFormat, ap);
    va_end(ap);
    if (aData) {
        self.Append(aData);
        PR_smprintf_free(aData);
    }
    else {
        MOZ_CRASH("Allocation or other failure in PR_vsmprintf");
    }
}


static void
__nsCString_Init__(nsACString &self, PyObject *src)
{
    Py_ssize_t aLength;
    const char *aData = nullptr;

    if (PyBytes_Check(src)) {
        aData = PyBytes_AS_STRING(src);
        aLength = PyBytes_GET_SIZE(src);
    }
    else {
        aData = PyUnicode_AsUTF8AndSize(src, &aLength);
    }
    if (aData) {
        if (aLength > UINT32_MAX) {
            PyErr_SetString(PyExc_OverflowError, "Python string is too long");
        }
        else {
            self.Assign(aData, aLength);
        }
    }
    else if (!PyErr_Occurred()) {
        self = EmptyCString();
    }
}


static void
nsCString_Init(nsACString &self, PyObject *src)
{
    if (!self.IsVoid()) {
        PyErr_SetString(PyExc_ValueError, "string is not void");
        self.SetIsVoid(true);
        return;
    }
    __nsCString_Init__(self, src);
}


static void
nsString_Init(nsAString &self, PyObject *src)
{
    if (!self.IsVoid()) {
        PyErr_SetString(PyExc_ValueError, "string is not void");
        self.SetIsVoid(true);
        return;
    }
    nsCString aSrc;
    aSrc.SetIsVoid(true);
    __nsCString_Init__(aSrc, src);
    if (!aSrc.IsVoid()) {
        CopyUTF8toUTF16(aSrc, self);
    }
}


/* --------------------------------------------------------------------------
   pyStackFrame
   -------------------------------------------------------------------------- */

class pyStackFrame final : public nsIStackFrame {
    public:
        NS_DECL_ISUPPORTS
        NS_DECL_NSISTACKFRAME

        pyStackFrame();

        static already_AddRefed<pyStackFrame> New(
            PyFrameObject *aFrame, long limit
        );

    protected:
        virtual ~pyStackFrame();

        bool Init(PyFrameObject *aFrame, long limit);

        nsString mFilename;
        nsString mName;
        int32_t mLineNumber;
        int32_t mColumnNumber;
        nsCString mSourceLine;
        nsCOMPtr<nsIStackFrame> mCaller;

        nsresult CallerToString(JSContext *aCx, nsACString &retval);
};


/* interface nsIStackFrame -------------------------------------------------- */

NS_IMPL_ISUPPORTS(pyStackFrame, nsIStackFrame)


NS_IMETHODIMP
pyStackFrame::GetLanguage(uint32_t *aLanguage)
{
    *aLanguage = nsIProgrammingLanguage::PYTHON;
    return NS_OK;
}


NS_IMETHODIMP
pyStackFrame::GetLanguageName(nsACString &aLanguageName)
{
    aLanguageName.AssignLiteral("Python");
    return NS_OK;
}


NS_IMETHODIMP
pyStackFrame::GetFilename(JSContext *aCx, nsAString &aFilename)
{
    aFilename.Assign(mFilename);
    return NS_OK;
}


NS_IMETHODIMP
pyStackFrame::GetName(JSContext *aCx, nsAString &aName)
{
    aName.Assign(mName);
    return NS_OK;
}


NS_IMETHODIMP
pyStackFrame::GetLineNumber(JSContext *aCx, int32_t *aLineNumber)
{
    *aLineNumber = mLineNumber;
    return NS_OK;
}


NS_IMETHODIMP
pyStackFrame::GetColumnNumber(JSContext *aCx, int32_t *aColumnNumber)
{
    *aColumnNumber = mColumnNumber;
    return NS_OK;
}


NS_IMETHODIMP
pyStackFrame::GetSourceLine(nsACString &aSourceLine)
{
    aSourceLine.Assign(mSourceLine);
    return NS_OK;
}


NS_IMETHODIMP
pyStackFrame::GetAsyncCause(JSContext *aCx, nsAString &aAsyncCause)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
pyStackFrame::GetAsyncCaller(JSContext *aCx, nsIStackFrame **aAsyncCaller)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
pyStackFrame::GetCaller(JSContext *aCx, nsIStackFrame **aCaller)
{
    NS_IF_ADDREF(*aCaller = mCaller);
    return NS_OK;
}


NS_IMETHODIMP
pyStackFrame::GetFormattedStack(JSContext *aCx, nsAString &aFormattedStack)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
pyStackFrame::GetNativeSavedFrame(JS::MutableHandleValue aNativeSavedFrame)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
pyStackFrame::ToString(JSContext *aCx, nsACString &retval)
{
    static const char nameFmt[] = ", in %s";

    nsresult rv = CallerToString(aCx, retval);
    NS_ENSURE_SUCCESS(rv, rv);
    if (!mFilename.IsEmpty()) {
        nsCString_AppendPrintf(
            retval, filenameFmt, NS_ConvertUTF16toUTF8(mFilename).get()
        );
        if (mLineNumber) {
            nsCString_AppendPrintf(retval, lineNumberFmt, mLineNumber);
        }
        if (!mName.IsEmpty()) {
            nsCString_AppendPrintf(
                retval, nameFmt, NS_ConvertUTF16toUTF8(mName).get()
            );
        }
        if (!mSourceLine.IsEmpty()) {
            nsCString_AppendPrintf(retval, sourceLineFmt, mSourceLine.get());
        }
    }
    return NS_OK;
}


/* Initialization/Finalization ---------------------------------------------- */

pyStackFrame::pyStackFrame()
{
    mFilename.SetIsVoid(true);
    mName.SetIsVoid(true);
    mLineNumber = 0;
    mColumnNumber = 0;
    mSourceLine.SetIsVoid(true);
    mCaller = nullptr;
}


pyStackFrame::~pyStackFrame()
{
}


bool
pyStackFrame::Init(PyFrameObject *aFrame, long limit)
{
    PyObject *sourceLine = nullptr;

    // linenumber
    mLineNumber = PyFrame_GetLineNumber(aFrame);
    if (aFrame->f_code) {
        // filename
        if (aFrame->f_code->co_filename) {
            nsString_Init(mFilename, aFrame->f_code->co_filename);
            if (mFilename.IsVoid()) {
                return false;
            }
            //sourceline
            if (!mFilename.IsEmpty() && mLineNumber) {
                if (!(sourceLine = _PyFrame_GetSourceLine(aFrame, mLineNumber))) {
                    return false;
                }
                nsCString_Init(mSourceLine, sourceLine);
                Py_DECREF(sourceLine);
                if (mSourceLine.IsVoid()) {
                    return false;
                }
                mSourceLine.Trim(" \t\r\n", true, true);
            }
        }
        //name
        if (aFrame->f_code->co_name) {
            nsString_Init(mName, aFrame->f_code->co_name);
            if (mName.IsVoid()) {
                return false;
            }
        }
    }
    // caller
    if (
        aFrame->f_back && limit &&
        !(mCaller = pyStackFrame::New(aFrame->f_back, --limit))
    ) {
        return false;
    }
    return true;
}


nsresult
pyStackFrame::CallerToString(JSContext *aCx, nsACString &retval)
{
    static const char callerFmt[] = "%s\n";
    nsCString aCaller;

    if (mCaller) {
        nsresult rv = mCaller->ToString(aCx, aCaller);
        NS_ENSURE_SUCCESS(rv, rv);
        nsCString_AppendPrintf(retval, callerFmt, aCaller.get());
    }
    return NS_OK;
}


already_AddRefed<pyStackFrame>
pyStackFrame::New(PyFrameObject *aFrame, long limit)
{
    RefPtr<pyStackFrame> frame = nullptr;

    frame = new (std::nothrow) pyStackFrame();
    if (!frame) {
        PyErr_NoMemory();
        return nullptr;
    }
    if (!frame->Init(aFrame, limit)) {
        return nullptr;
    }
    return frame.forget();
}


/* --------------------------------------------------------------------------
   pyException
   -------------------------------------------------------------------------- */

class pyException : public nsIException {
    public:
        NS_DECL_ISUPPORTS
        NS_DECL_NSIEXCEPTION

        pyException();

        bool Init(
            JSContext *aCx, PyObject *aValue, nsIStackFrame *aLocation,
            nsIException *aMaybeCause
        );

    protected:
        virtual ~pyException();

        nsCString mMessage;
        nsresult mResult;
        nsCString mName;
        nsString mFilename;
        uint32_t mLineNumber;
        uint32_t mColumnNumber;
        nsCOMPtr<nsIStackFrame> mLocation;

        nsCOMPtr<nsIException> mCause;
        nsCOMPtr<nsIException> mContext;

        virtual PyObject *GetMessage(PyObject *aValue);
        virtual bool SetLocation(
            JSContext *aCx, PyObject *aValue, nsIStackFrame *aLocation
        );
        virtual nsresult LocationToString(JSContext *aCx, nsACString &retval);

        bool SetName(PyObject *aValue);
        bool SetCauseOrContext(
            JSContext *aCx, PyObject *aValue, nsIException *aMaybeCause
        );
        nsresult CauseToString(JSContext *aCx, nsACString &retval);
        nsresult ContextToString(JSContext *aCx, nsACString &retval);
};


/* interface nsIException --------------------------------------------------- */

NS_IMPL_ISUPPORTS(pyException, nsIException)


NS_IMETHODIMP
pyException::GetMessageMoz(nsACString &aMessage)
{
    aMessage.Assign(mMessage);
    return NS_OK;
}


NS_IMETHODIMP
pyException::GetResult(nsresult *aResult)
{
    *aResult = mResult;
    return NS_OK;
}


NS_IMETHODIMP
pyException::GetName(nsACString &aName)
{
    aName.Assign(mName);
    return NS_OK;
}


NS_IMETHODIMP
pyException::GetFilename(JSContext *aCx, nsAString &aFilename)
{
    if (mLocation) {
        return mLocation->GetFilename(aCx, aFilename);
    }
    aFilename.Assign(mFilename);
    return NS_OK;
}


NS_IMETHODIMP
pyException::GetLineNumber(JSContext *aCx, uint32_t *aLineNumber)
{
    if (mLocation) {
        int32_t lineNumber;
        nsresult rv = mLocation->GetLineNumber(aCx, &lineNumber);
        *aLineNumber = lineNumber;
        return rv;
    }
    *aLineNumber = mLineNumber;
    return NS_OK;
}


NS_IMETHODIMP
pyException::GetColumnNumber(uint32_t *aColumnNumber)
{
    if (mLocation) {
        int32_t columnNumber;
        nsresult rv = mLocation->GetColumnNumber(nullptr, &columnNumber);
        *aColumnNumber = columnNumber;
        return rv;
    }
    *aColumnNumber = mColumnNumber;
    return NS_OK;
}


NS_IMETHODIMP
pyException::GetLocation(nsIStackFrame **aLocation)
{
    NS_IF_ADDREF(*aLocation = mLocation);
    return NS_OK;
}


NS_IMETHODIMP
pyException::GetData(nsISupports **aData)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}


NS_IMETHODIMP
pyException::ToString(JSContext *aCx, nsACString &retval)
{
    nsresult rv = CauseToString(aCx, retval);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = LocationToString(aCx, retval);
    NS_ENSURE_SUCCESS(rv, rv);
    retval.Append(mName);
    if (!mMessage.IsEmpty()) {
        nsCString_AppendPrintf(retval, messageFmt, mMessage.get());
    }
    return ContextToString(aCx, retval);
}


/* Initialization/Finalization ---------------------------------------------- */

pyException::pyException()
{
    mMessage.SetIsVoid(true);
    mResult = NS_ERROR_FAILURE;
    mName.SetIsVoid(true);
    mFilename.SetIsVoid(true);
    mLineNumber = 0;
    mColumnNumber = 0;
    mLocation = nullptr;
    mCause = nullptr;
    mContext = nullptr;
}


pyException::~pyException()
{
}


bool
pyException::Init(
    JSContext *aCx, PyObject *aValue, nsIStackFrame *aLocation,
    nsIException *aMaybeCause
)
{
    PyObject *message = nullptr;

    if ((message = GetMessage(aValue))) {
        nsCString_Init(mMessage, message);
        Py_DECREF(message);
    }
    if (
        mMessage.IsVoid() ||
        !SetName(aValue) ||
        !SetLocation(aCx, aValue, aLocation) ||
        !SetCauseOrContext(aCx, aValue, aMaybeCause)
    ) {
        return false;
    }
    return true;
}


PyObject *
pyException::GetMessage(PyObject *aValue)
{
    return PyObject_Str(aValue);
}


bool
pyException::SetLocation(
    JSContext *aCx, PyObject *aValue, nsIStackFrame *aLocation
)
{
    PyObject *aTraceback = nullptr;
    PyTracebackObject *tb = nullptr;
    long limit = -1;

    if ((aTraceback = PyException_GetTraceback(aValue))) {
        if (PyTraceBack_Check(aTraceback)) {
            tb = (PyTracebackObject *)aTraceback;
            while(tb->tb_next) {
                /*
                // XXX: fix frames, this needs a better fix
                // XXX: is it still necessary?
                if (!(tb->tb_frame && tb->tb_next->tb_frame)) {
                    PyErr_SetString(PyExc_SystemError, "traceback without frame");
                    Py_DECREF(aTraceback);
                    return false;
                }
                tb->tb_next->tb_frame->f_back = tb->tb_frame;
                */
                tb = tb->tb_next;
            }
            if (
                (limit = _PyTraceBack_GetLimit()) < 0 ||
                !(mLocation = pyStackFrame::New(tb->tb_frame, limit))
            ) {
                Py_DECREF(aTraceback);
                return false;
            }
            Py_DECREF(aTraceback);
            return true;
        }
        Py_DECREF(aTraceback);
    }
    if (aLocation) {
        PY_ENSURE_SUCCESS(
            aLocation->GetFilename(aCx, mFilename), false,
            XPCOMError, "Failed to get filename"
        );
        int32_t aLineNumber;
        PY_ENSURE_SUCCESS(
            aLocation->GetLineNumber(aCx, &aLineNumber), false,
            XPCOMError, "Failed to get lineNumber"
        );
        mLineNumber = aLineNumber;
        int32_t aColumnNumber;
        PY_ENSURE_SUCCESS(
            aLocation->GetColumnNumber(aCx, &aColumnNumber), false,
            XPCOMError, "Failed to get columnNumber"
        );
        mColumnNumber = aColumnNumber;
    }
    return true;
}


bool
pyException::SetName(PyObject *aValue)
{
    _Py_IDENTIFIER(__module__);
    PyObject *aType = PyExceptionInstance_Class(aValue);
    PyObject *moduleName = nullptr;
    const char *typeName = nullptr;

    if ((moduleName = _PyObject_GetAttrId(aType, &PyId___module__))) {
        nsCString_Init(mName, moduleName);
        Py_DECREF(moduleName);
    }
    if (mName.IsVoid()) {
        return false;
    }
    typeName = PyExceptionClass_Name(aType);
    if (mName == "builtins" || mName == "__main__") {
        mName.Assign(typeName);
    }
    else {
        nsCString_AppendPrintf(mName, ".%s", typeName);
    }
    return true;
}


bool
pyException::SetCauseOrContext(
    JSContext *aCx, PyObject *aValue, nsIException *aMaybeCause
)
{
    PyObject *aCause = nullptr, *aContext = nullptr;

    if (aMaybeCause) {
        mCause = aMaybeCause;
    }
    else if ((aCause = PyException_GetCause(aValue))) {
        if (!(mCause = Exception(aCx, aCause, nullptr, nullptr))) {
            Py_DECREF(aCause);
            return false;
        }
        Py_DECREF(aCause);
    }
    else if ((aContext = PyException_GetContext(aValue))) {
        if (
            !((PyBaseExceptionObject *)aValue)->suppress_context &&
            !(mContext = Exception(aCx, aContext, nullptr, nullptr))
        ) {
            Py_DECREF(aContext);
            return false;
        }
        Py_DECREF(aContext);
    }
    return true;
}


nsresult
pyException::LocationToString(JSContext *aCx, nsACString &retval)
{
    static const char locationFmt[] =
        "Traceback (most recent call last):\n%s\n";
    nsCString aLocation;

    if (mLocation) {
        nsresult rv = mLocation->ToString(aCx, aLocation);
        NS_ENSURE_SUCCESS(rv, rv);
        nsCString_AppendPrintf(retval, locationFmt, aLocation.get());
    }
    return NS_OK;
}


nsresult
pyException::CauseToString(JSContext *aCx, nsACString &retval)
{
    static const char causeFmt[] =
        "%s\nThe above exception might have been the direct cause "
        "of the following exception:\n\n";
    nsCString aCause;

    if (mCause) {
        nsresult rv = mCause->ToString(aCx, aCause);
        NS_ENSURE_SUCCESS(rv, rv);
        nsCString_AppendPrintf(retval, causeFmt, aCause.get());
    }
    return NS_OK;
}


nsresult
pyException::ContextToString(JSContext *aCx, nsACString &retval)
{
    static const char contextFmt[] =
        "\n\nDuring handling of the above exception, "
        "another exception occurred:\n%s";
    nsCString aContext;

    if (mContext) {
        nsresult rv = mContext->ToString(aCx, aContext);
        NS_ENSURE_SUCCESS(rv, rv);
        nsCString_AppendPrintf(retval, contextFmt, aContext.get());
    }
    return NS_OK;
}


/* --------------------------------------------------------------------------
   pySyntaxError
   -------------------------------------------------------------------------- */

class pySyntaxError final : public pyException {
    public:
        pySyntaxError();

    protected:
        virtual ~pySyntaxError();

        nsCString mSourceLine;

        PyObject *GetMessage(PyObject *aValue) override;
        bool SetLocation(
            JSContext *aCx, PyObject *aValue, nsIStackFrame *aLocation
        ) override;
        nsresult LocationToString(JSContext *aCx, nsACString &retval) override;

        bool SetFilename(PyObject *aValue);
        bool SetLineNumber(PyObject *aValue);
        bool SetColumnNumber(PyObject *aValue);
        bool SetSourceLine(PyObject *aValue);
};


/* Initialization/Finalization ---------------------------------------------- */

pySyntaxError::pySyntaxError()
{
    mSourceLine.SetIsVoid(true);
}


pySyntaxError::~pySyntaxError()
{
}


PyObject *
pySyntaxError::GetMessage(PyObject *aValue)
{
    _Py_IDENTIFIER(msg);

    if (
        SetFilename(aValue) &&
        SetLineNumber(aValue) &&
        SetColumnNumber(aValue) &&
        SetSourceLine(aValue)
    ) {
        return _PyObject_GetAttrId(aValue, &PyId_msg);
    }
    return nullptr;
}


bool
pySyntaxError::SetLocation(
    JSContext *aCx, PyObject *aValue, nsIStackFrame *aLocation
)
{
    return true;
}


nsresult
pySyntaxError::LocationToString(JSContext *aCx, nsACString &retval)
{
    static const char columnNumberFmt[] = "\n    %*c";
    static const char indicator = '^';

    if (!mFilename.IsEmpty()) {
        nsCString_AppendPrintf(
            retval, filenameFmt, NS_ConvertUTF16toUTF8(mFilename).get()
        );
        if (mLineNumber) {
            nsCString_AppendPrintf(retval, lineNumberFmt, mLineNumber);
        }
        if (!mSourceLine.IsEmpty()) {
            nsCString_AppendPrintf(retval, sourceLineFmt, mSourceLine.get());
            if (mColumnNumber) {
                nsCString_AppendPrintf(
                    retval, columnNumberFmt, mColumnNumber, indicator
                );
            }
        }
        retval.Append("\n");
    }
    return NS_OK;
}


bool
pySyntaxError::SetFilename(PyObject *aValue)
{
    _Py_IDENTIFIER(filename);
    PyObject *filename = nullptr;

    if ((filename = _PyObject_GetAttrId(aValue, &PyId_filename))) {
        nsString_Init(mFilename, filename);
        Py_DECREF(filename);
    }
    if (mFilename.IsVoid()) {
        return false;
    }
    return true;
}


bool
pySyntaxError::SetLineNumber(PyObject *aValue)
{
    _Py_IDENTIFIER(lineno);
    PyObject *lineno = nullptr;
    long aLineNumber = -1;

    if ((lineno = _PyObject_GetAttrId(aValue, &PyId_lineno))) {
        aLineNumber = PyLong_AsLong(lineno);
        if (aLineNumber < 0) {
            if (!PyErr_Occurred()) {
                PyErr_SetString(
                    PyExc_ValueError, "'lineNumber' cannot be negative"
                );
            }
        }
        else {
            mLineNumber = aLineNumber;
        }
        Py_DECREF(lineno);
    }
    if (PyErr_Occurred()) {
        return false;
    }
    return true;
}


bool
pySyntaxError::SetColumnNumber(PyObject *aValue)
{
    _Py_IDENTIFIER(offset);
    PyObject *offset = nullptr;
    long aColumnNumber = -1;

    if ((offset = _PyObject_GetAttrId(aValue, &PyId_offset))) {
        if (offset != Py_None) {
            aColumnNumber = PyLong_AsLong(offset);
            if (aColumnNumber < 0) {
                if (!PyErr_Occurred()) {
                    PyErr_SetString(
                        PyExc_ValueError, "'columnNumber' cannot be negative"
                    );
                }
            }
            else {
                mColumnNumber = aColumnNumber;
            }
        }
        Py_DECREF(offset);
    }
    if (PyErr_Occurred()) {
        return false;
    }
    return true;
}


bool
pySyntaxError::SetSourceLine(PyObject *aValue)
{
    _Py_IDENTIFIER(text);
    PyObject *text = nullptr;
    uint32_t oldLength = 0, newLength = 0;

    if ((text = _PyObject_GetAttrId(aValue, &PyId_text))) {
        nsCString_Init(mSourceLine, text);
        Py_DECREF(text);
    }
    if (mSourceLine.IsVoid()) {
        return false;
    }
    if (!mSourceLine.IsEmpty()) {
        if (mColumnNumber) {
            oldLength = mSourceLine.Length();
            mSourceLine.Trim(" \t\f", true, false);
            mColumnNumber -= (oldLength - mSourceLine.Length());
            mSourceLine.Trim("\r\n", false, true);
            newLength = mSourceLine.Length();
            if (mColumnNumber > newLength) {
                mColumnNumber = newLength;
            }
        }
        else {
            mSourceLine.Trim(" \t\r\n", true, true);
        }
    }
    return true;
}


} // namespace anonymous


/* Exception ---------------------------------------------------------------- */

already_AddRefed<nsIException>
Exception(
    JSContext *aCx, PyObject *aValue, nsIStackFrame *aLocation,
    nsIException *aMaybeCause
)
{
    _Py_IDENTIFIER(print_file_and_line);
    RefPtr<pyException> aException = nullptr;

    if (_PyObject_HasAttrId(aValue, &PyId_print_file_and_line)) {
        aException = new (std::nothrow) pySyntaxError();
    }
    else {
        aException = new (std::nothrow) pyException();
    }
    if (!aException) {
        PyErr_NoMemory();
        return nullptr;
    }
    if (!aException->Init(aCx, aValue, aLocation, aMaybeCause)) {
        return nullptr;
    }
    return aException.forget();
}


/* Warning ------------------------------------------------------------------ */

already_AddRefed<nsIScriptError>
Warning(
    PyObject *aMessage, PyObject *aCategory, PyObject *aFilename,
    PyObject *aLineno, PyObject *aLine
)
{
    nsresult rv = NS_ERROR_FAILURE;
    nsCString message, sourceLine, category, warningMsg;
    nsString fileName;
    PyObject *tmp = nullptr;
    long lineNumber;
    nsCOMPtr<nsIScriptError> aWarning;

    // message
    message.SetIsVoid(true);
    if ((tmp = PyObject_Str(aMessage))) {
        nsCString_Init(message, tmp);
        Py_CLEAR(tmp);
    }
    if (message.IsVoid()) {
        return nullptr;
    }
    // fileName
    fileName.SetIsVoid(true);
    nsString_Init(fileName, aFilename);
    if (fileName.IsVoid()) {
        return nullptr;
    }
    // sourceLine
    sourceLine.SetIsVoid(true);
    if (aLine == Py_None) {
        if ((tmp = _Py_GetSourceLine(aFilename, aLineno))) {
            nsCString_Init(sourceLine, tmp);
            Py_CLEAR(tmp);
        }
    }
    else {
        nsCString_Init(sourceLine, aLine);
    }
    if (sourceLine.IsVoid()) {
        return nullptr;
    }
    sourceLine.Trim(" \t\r\n", true, true);
    // lineNumber
    if ((lineNumber = PyLong_AsLong(aLineno)) == -1 && PyErr_Occurred()) {
        return nullptr;
    }
    // category
    category.Assign(PyExceptionClass_Name(aCategory));
    // format message
    warningMsg.Assign(NS_ConvertUTF16toUTF8(fileName).get());
    if (!warningMsg.IsEmpty()) {
        if (lineNumber > 0) {
            nsCString_AppendPrintf(warningMsg, lineNumberFmt, lineNumber);
        }
        warningMsg.Append(": ");
    }
    warningMsg.Append(category);
    if (!message.IsEmpty()) {
        nsCString_AppendPrintf(warningMsg, messageFmt, message.get());
    }
    if (!sourceLine.IsEmpty()) {
        nsCString_AppendPrintf(warningMsg, "\n  %s", sourceLine.get());
    }
    // create and init nsIScriptError
    aWarning = do_CreateInstance(NS_SCRIPTERROR_CONTRACTID, &rv);
    PY_ENSURE_SUCCESS(
        rv, nullptr, XPCOMError, "Failed to create nsIScriptError"
    );
    rv = aWarning->InitWithWindowID(
        NS_ConvertUTF8toUTF16(warningMsg), fileName, EmptyString(),
        lineNumber, 0, nsIScriptError::warningFlag, category, 0
    );
    PY_ENSURE_SUCCESS(
        rv, nullptr, XPCOMError, "Failed to initialize nsIScriptError"
    );
    return aWarning.forget();
}


/* Initialize/Finalize ------------------------------------------------------ */

PyObject *Error = nullptr;
PyObject *XPCOMError = nullptr;
PyObject *JSError = nullptr;
PyObject *JSWarning = nullptr;


bool
Initialize()
{
    if (
        (Error = PyErr_NewException("pyxul.Error", nullptr, nullptr)) &&
        (XPCOMError = PyErr_NewException("pyxul.XPCOMError", Error, nullptr)) &&
        (JSError = PyErr_NewException("pyxul.JSError", Error, nullptr)) &&
        (JSWarning = PyErr_NewException("pyxul.JSWarning", PyExc_Warning, nullptr))
    ) {
        return true;
    }
    return false;
}


void
Finalize()
{
    Py_CLEAR(JSWarning);
    Py_CLEAR(JSError);
    Py_CLEAR(XPCOMError);
    Py_CLEAR(Error);
}


} // namespace pyxul::errors

