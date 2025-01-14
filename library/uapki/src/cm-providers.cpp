/*
 * Copyright (c) 2022, The UAPKI Project Authors.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "api-json-internal.h"
#include "cm-providers.h"
#include "cm-storage-proxy.h"
#include "parson-helper.h"
#include <string>
#include <vector>


#define DEBUG_OUTCON(expression)
#ifndef DEBUG_OUTCON
#define DEBUG_OUTCON(expression) expression
#endif


using namespace std;


typedef struct CM_PROVIDER_ST {
    const string    id;
    CmStorageProxy* storage;
    CM_PROVIDER_ST (const string& iId, CmStorageProxy* const iCmSession)
        : id(iId), storage(iCmSession) {
    }
} CM_PROVIDER;


static struct LIB_CMPROVIDERS_ST {
    const CM_PROVIDER* 
                activeProvider;
    CmStorageProxy*
                activeStorage;
    vector<CM_PROVIDER>
                providers;

    LIB_CMPROVIDERS_ST (void) {
        setStorage(nullptr, nullptr);
    }

    bool storageIsOpen (void) const {
        return (activeStorage != nullptr);
    }
    void setStorage (CM_PROVIDER* provider, CmStorageProxy* storage) {
        activeProvider = provider;
        activeStorage = storage;
    }
} lib_cmproviders;


int CmProviders::loadProvider (const string& dir, const string& libName, const char* jsonParams)
{
    CmStorageProxy* storage = new CmStorageProxy();
    if (!storage) return RET_UAPKI_GENERAL_ERROR;

    int ret = RET_OK;
    const string s_initparams = string((jsonParams) ? jsonParams : "");

    DEBUG_OUTCON(printf("CmProviders::loadProvider(name: '%s', dir: '%s', params: '%s')\n", libName.c_str(), dir.c_str(), s_initparams.c_str()));
    if (!storage->load(libName, dir)) {
        SET_ERROR(RET_CM_LIBRARY_NOT_LOADED);
    }

    ret = storage->providerInit(s_initparams);
    DEBUG_OUTCON(printf("CmProviders::loadProvider: session->providerInit(), ret: %d\n", ret));
    if (ret == RET_OK) {
        string s_id, s_provinfo;
        DO(storage->providerInfo(s_provinfo));

        ParsonHelper json;
        if (!json.parse(s_provinfo.c_str())) {
            SET_ERROR(RET_UAPKI_INVALID_JSON_FORMAT);
        }

        json.getString("id", s_id);
        if (s_id.empty()) {
            SET_ERROR(RET_UAPKI_INVALID_JSON_FORMAT);
        }

        lib_cmproviders.providers.push_back(CM_PROVIDER(s_id, storage));
        storage = nullptr;
    }

cleanup:
    delete storage;
    return ret;
}

void CmProviders::deinit (void)
{
    for (auto& it : lib_cmproviders.providers) {
        delete it.storage;
        it.storage = nullptr;
    }
    lib_cmproviders.providers.clear();
}

size_t CmProviders::count (void)
{
    return lib_cmproviders.providers.size();
}

int CmProviders::getInfo (const size_t index, JSON_Object* joResult)
{
    if (index >= CmProviders::count()) return RET_INVALID_PARAM;

    int ret = RET_OK;
    CmStorageProxy* storage = lib_cmproviders.providers[index].storage;
    string s_provinfo;
    ParsonHelper json;

    DO(storage->providerInfo(s_provinfo));

    if (!json.parse(s_provinfo.c_str())) {
        SET_ERROR(RET_UAPKI_INVALID_JSON_FORMAT);
    }

    DO_JSON(json_object_set_string(joResult, "id",           json.getString("id")));
    DO_JSON(json_object_set_string(joResult, "apiVersion",   json.getString("apiVersion")));
    DO_JSON(json_object_set_string(joResult, "libVersion",   json.getString("libVersion")));
    DO_JSON(json_object_set_string(joResult, "description",  json.getString("description")));
    DO_JSON(json_object_set_string(joResult, "manufacturer", json.getString("manufacturer")));
    DO_JSON(ParsonHelper::jsonObjectSetBoolean(joResult, "supportListStorages",
                                                             json.getBoolean("supportListStorages", false)));
    if (json.hasValue("flags", JSONNumber)) {
        const int flags = json.getInt("flags");
        if ((flags >= 0) && (flags <= 0xFFFFFFFF)) ParsonHelper::jsonObjectSetUint32(joResult, "flags", (uint32_t)flags);
    }

cleanup:
    return ret;
}

struct CM_PROVIDER_ST* CmProviders::getProviderById (const char* id)
{
    const string s_id = string(id ? id : "");
    for (size_t i = 0; i < lib_cmproviders.providers.size(); i++) {
        CM_PROVIDER* provider = &lib_cmproviders.providers[i];
        if (s_id == provider->id) {
            return provider;
        }
    }
    return nullptr;
}

int CmProviders::listStorages (const char* providerId, JSON_Object* joResult)
{
    CM_PROVIDER* cm_provider = CmProviders::getProviderById(providerId);
    if (!cm_provider) return RET_UAPKI_UNKNOWN_PROVIDER;

    CmStorageProxy* storage = cm_provider->storage;
    string s_storlist;
    int ret = storage->storageList(s_storlist);
    if (ret != RET_OK) return ret;

    ParsonHelper json;
    JSON_Object* jo_resp = json.parse(s_storlist.c_str());
    if (!jo_resp) return RET_UAPKI_INVALID_JSON_FORMAT;

    ret = (json_object_copy_all_items(joResult, jo_resp) == JSONSuccess) ? RET_OK : RET_UAPKI_JSON_FAILURE;
    return ret;
}

int CmProviders::storageInfo (const char* providerId, const char* storageId, JSON_Object* joResult)
{
    CM_PROVIDER* cm_provider = CmProviders::getProviderById(providerId);
    if (!cm_provider) return RET_UAPKI_UNKNOWN_PROVIDER;

    CmStorageProxy* storage = cm_provider->storage;
    string s_storinfo;
    const string s_storageid = string(storageId ? storageId : "");
    int ret = storage->storageInfo(s_storageid, s_storinfo);
    if (ret != RET_OK) return ret;

    ParsonHelper json;
    JSON_Object* jo_resp = json.parse(s_storinfo.c_str());
    if (!jo_resp) return RET_UAPKI_INVALID_JSON_FORMAT;

    ret = (json_object_copy_all_items(joResult, jo_resp) == JSONSuccess) ? RET_OK : RET_UAPKI_JSON_FAILURE;
    return ret;
}

int CmProviders::storageOpen (const char* providerId, const char* storageId, JSON_Object* joParams)
{
    const string s_mode = ParsonHelper::jsonObjectGetString(joParams, "mode");
    const string s_password = ParsonHelper::jsonObjectGetString(joParams, "password");
    const char* s_username = json_object_get_string(joParams, "username");
    if (s_password.empty()) return RET_UAPKI_INVALID_PARAMETER;

    CM_OPEN_MODE mode = OPEN_MODE_RW;
    if ((s_mode == "RW") || s_mode.empty()) mode = OPEN_MODE_RW;
    else if (s_mode == "RO") mode = OPEN_MODE_RO;
    else if (s_mode == "CREATE") mode = OPEN_MODE_CREATE;
    else return RET_UAPKI_INVALID_PARAMETER;

    CM_PROVIDER* cm_provider = CmProviders::getProviderById(providerId);
    if (!cm_provider) return RET_UAPKI_UNKNOWN_PROVIDER;

    CmStorageProxy* storage = cm_provider->storage;
    string s_openparams;
    if (ParsonHelper::jsonObjectHasValue(joParams, "openParams", JSONObject)) {
        ParsonHelper json;
        json_object_copy_all_items(json.create(), json_object_get_object(joParams, "openParams"));
        json.serialize(s_openparams);
    }

    int ret = storage->storageOpen(storageId, mode, s_openparams);
    if (ret != RET_OK) return ret;

    ret = storage->sessionLogin(s_password.c_str(), s_username);
    if (ret == RET_OK) {
        lib_cmproviders.setStorage(cm_provider, storage);
    }
    else {
        (void)storage->storageClose();
    }
    return ret;
}

int CmProviders::storageClose (void)
{
    int ret = RET_OK;
    if (lib_cmproviders.storageIsOpen()) {
        ret = lib_cmproviders.activeStorage->storageClose();
        lib_cmproviders.setStorage(nullptr, nullptr);
    }
    else {
        ret = RET_UAPKI_STORAGE_NOT_OPEN;
    }
    return ret;
}

CmStorageProxy* CmProviders::openedStorage (void)
{
    return lib_cmproviders.activeStorage;
}
