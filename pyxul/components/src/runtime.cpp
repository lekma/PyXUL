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


#include "runtime.h"

#include "xpc.h"

#include "nsIDOMWindowUtils.h"
#include "nsIObserverService.h"

#include "mozilla/ModuleUtils.h"


#define PY_RUNTIME_CLEANUP_TOPIC "dom-window-destroyed"
#define PY_RUNTIME_FINALIZE_TOPIC "xpcom-will-shutdown"


namespace pyxul {


/* --------------------------------------------------------------------------
   pyRuntime implementation
   -------------------------------------------------------------------------- */

NS_IMPL_ISUPPORTS(pyRuntime, pyIRuntime, nsIObserver, mozilla::ModuleLoader)


mozilla::StaticRefPtr<pyRuntime> pyRuntime::sRuntime;


/* interface pyIRuntime ----------------------------------------------------- */

NS_IMETHODIMP
pyRuntime::Execute(
    nsIDOMWindow *aDOMWindow, const nsACString &aSrc,
    const char *aOriginCharset, nsIURI *aBaseURI
)
{
    nsresult rv = NS_ERROR_FAILURE;
    uint64_t aWindowID = 0;

    if (aSrc.IsEmpty()) {
        return NS_OK;
    }
    nsCOMPtr<nsIDOMWindowUtils> aUtils = do_GetInterface(aDOMWindow, &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aUtils->GetOuterWindowID(&aWindowID);
    NS_ENSURE_SUCCESS(rv, rv);
    if (aWindowID && !mWindowIDs.Contains(aWindowID)) {
        mWindowIDs.AppendElement(aWindowID);
    }
    return xpc::Execute(aDOMWindow, aSrc, aOriginCharset, aBaseURI);
}


/* interface nsIObserver ---------------------------------------------------- */

NS_IMETHODIMP
pyRuntime::Observe(nsISupports *aSubject, const char *aTopic,const char16_t *aData)
{
    if (!strcmp(aTopic, PY_RUNTIME_CLEANUP_TOPIC)) {
        nsCOMPtr<nsIDOMWindow> aDOMWindow = do_GetInterface(aSubject);
        if (aDOMWindow) {
            nsCOMPtr<nsIDOMWindowUtils> aUtils = do_GetInterface(aDOMWindow);
            if (aUtils) {
                uint64_t aWindowID = 0;
                if (
                    NS_SUCCEEDED(aUtils->GetOuterWindowID(&aWindowID)) &&
                    aWindowID && mWindowIDs.Contains(aWindowID)
                ) {
                    mWindowIDs.RemoveElement(aWindowID);
                    xpc::Cleanup(aDOMWindow);
                }
            }
        }
    }
    else if (!strcmp(aTopic, PY_RUNTIME_FINALIZE_TOPIC) && sRuntime) {
        sRuntime->Finalize();
        sRuntime = nullptr;
    }
    return NS_OK;
}


/* interface mozilla::ModuleLoader ------------------------------------------ */

const mozilla::Module *
pyRuntime::LoadModule(mozilla::FileLocation &aFileLocation)
{
    return xpc::LoadModule(aFileLocation);
}


/* Initialization/Finalization ---------------------------------------------- */

/* ctor */
pyRuntime::pyRuntime() : mPythonLibrary(nullptr)
{
}


/* dtor */
pyRuntime::~pyRuntime()
{
}


bool
pyRuntime::Initialize()
{
    nsresult rv = NS_ERROR_FAILURE;
    char *libname = nullptr;
    PRLibSpec libspec;

    // setup observers
    nsCOMPtr<nsIObserverService> aObserverService = GetObserverService();
    NS_ENSURE_TRUE(aObserverService, false);
    rv = aObserverService->AddObserver(this, PY_RUNTIME_FINALIZE_TOPIC, false);
    NS_ENSURE_SUCCESS(rv, false);
    rv = aObserverService->AddObserver(this, PY_RUNTIME_CLEANUP_TOPIC, false);
    NS_ENSURE_SUCCESS(rv, false);

    // load Python
    if (!(libname = PR_GetLibraryName(nullptr, PYTHON_LIBNAME))) {
        return false;
    }
    libspec.type = PR_LibSpec_Pathname;
    libspec.value.pathname = libname;
    mPythonLibrary = PR_LoadLibraryWithFlags(libspec, PR_LD_LAZY | PR_LD_GLOBAL);
    PR_FreeLibraryName(libname);
    return mPythonLibrary ? xpc::Initialize() : false;
}


void
pyRuntime::Finalize()
{
    mWindowIDs.Clear();

    // unload Python
    if (mPythonLibrary) {
        xpc::Finalize();
        PR_UnloadLibrary(mPythonLibrary);
        mPythonLibrary = nullptr;
    }

    // remove observers
    nsCOMPtr<nsIObserverService> aObserverService = GetObserverService();
    if (aObserverService) {
        aObserverService->RemoveObserver(this, PY_RUNTIME_CLEANUP_TOPIC);
        aObserverService->RemoveObserver(this, PY_RUNTIME_FINALIZE_TOPIC);
    }
}


/* mozilla Module constructor */
already_AddRefed<pyRuntime>
pyRuntime::GetRuntime()
{
    RefPtr<pyRuntime> aRuntime = nullptr;

    if (sRuntime) {
        aRuntime = sRuntime.get();
    }
    else {
        aRuntime = new (std::nothrow) pyRuntime();
        if (aRuntime) {
            if (!aRuntime->Initialize()) {
                aRuntime->Finalize();
                return nullptr;
            }
            sRuntime = aRuntime;
        }
    }
    return aRuntime.forget();
}

NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(pyRuntime, pyRuntime::GetRuntime)


} // namespace pyxul


/* --------------------------------------------------------------------------
   mozilla Module implementation
   -------------------------------------------------------------------------- */

NS_DEFINE_NAMED_CID(PY_RUNTIME_CID);


static const mozilla::Module::CIDEntry kPythonCIDs[] = {
    {
        &kPY_RUNTIME_CID, true, nullptr, pyxul::pyRuntimeConstructor,
        Module::MAIN_PROCESS_ONLY
    },
    {nullptr }
};


static const mozilla::Module::ContractIDEntry kPythonContractIDs[] = {
    {PY_RUNTIME_CONTRACTID, &kPY_RUNTIME_CID, Module::MAIN_PROCESS_ONLY},
    {nullptr}
};


static const mozilla::Module::CategoryEntry kPythonCategories[] = {
    {"module-loader", "py", PY_RUNTIME_CONTRACTID},
    {"agent-style-sheets", "Python Bindings", "chrome://pyxul/content/pyxul.css"},
    {nullptr}
};


/* PythonModule */
static const mozilla::Module kPythonModule = {
    .mVersion = mozilla::Module::kVersion,
    .mCIDs = kPythonCIDs,
    .mContractIDs = kPythonContractIDs,
    .mCategoryEntries = kPythonCategories,
    .selector = Module::MAIN_PROCESS_ONLY,
};


NSMODULE_DEFN(PythonModule) = &kPythonModule;

