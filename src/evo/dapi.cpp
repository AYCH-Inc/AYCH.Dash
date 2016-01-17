

// Copyright (c) 2014-2015 The Dash developers

// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "main.h"
#include "core_io.h"
#include "db.h"
#include "dapi.h"
#include "file.h"
#include "json/json_spirit.h"
#include "json/json_spirit_value.h"
#include "easywsclient.h"
#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>

#include <string>
#include <iostream>
#include <cstdio>
#include <memory>

int nError;
std::string strErrorMessage;
// error reporting

using easywsclient::WebSocket;
WebSocket::pointer ws_client; 

std::string GetIndexFile(std::string strFilename)
{
    boost::filesystem::path filename = GetDataDirectory() / "index" / strFilename;
    return filename.c_str();
}

std::string GetProfileFile(std::string strUID)
{
    boost::filesystem::path filename = GetDataDirectory() / "users" / strUID;
    return filename.c_str();
}

std::string GetPrivateDataFile(std::string strUID, int nSlot)
{
    std::string strFilename = strUID + "." + boost::lexical_cast<std::string>(nSlot);
    boost::filesystem::path filename = GetDataDirectory() / "users" / strFilename;
    return filename.c_str();
}

bool IsValidUsername(std::string strUsername)
{
    for(std::string::size_type i = 0; i < strUsername.size(); ++i) {
        if(!std::isalpha(strUsername[i]) && !std::isdigit(strUsername[i]) && strUsername[i] != '_') return false;
    }
    return true;
}

// std::string exec(const char* cmd) {
//     std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
//     if (!pipe) return "ERROR";
//     char buffer[128];
//     std::string result = "";
//     while (!feof(pipe.get())) {
//         if (fgets(buffer, 128, pipe.get()) != NULL)
//             result += buffer;
//     }
//     return result;
// }

void EventNotify(const std::string& strEvent)
{
    std::string strCmd = GetArg("-eventnotify", "");

    //boost::replace_all(strCmd, "%s", strEvent);
    //boost::thread t(runCommand, strCmd); // thread runs free

    ws_client = WebSocket::from_url("ws://localhost:5000/");
    
    assert(ws_client);
    ws_client->send(strEvent);
    ws_client->poll();

    printf("sending out: %s\n", strEvent.c_str());

    ws_client->close();
}

std::string escapeJsonString(const std::string& input) {
    // NOTE: Any ideas on replacing this with something more portable? 

    std::ostringstream ss;
    for (std::string::const_iterator iter = input.begin(); iter != input.end(); iter++) {
        switch (*iter) {
            case '\\': ss << "\\\\"; break;
            case '"': ss << "\\\""; break;
            case '/': ss << "\\/"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default: ss << *iter; break;
        }
    }
    return ss.str();
}


void ResetErrorStatus() {nError = 0; strErrorMessage = "";}
void SetError(int nErrorIn, std::string strMessageIn) {nError = nErrorIn; strErrorMessage = strMessageIn;}

/*

{ 
  "object" : "dash_budget_items",
  "data" : [
    {  "object" : "dash_budget",
    "name" : "ds_liquidity",
    ...
  },
    {  "object" : "dash_budget",
    "name" : "christmas-lottery",
    ...
  },
  ...
  ]
}
*/

Object GetResultObject(std::string strID, std::string strCommand, Object& objFile)
{
    Object retData;
    retData.push_back(Pair("id", strID));
    retData.push_back(Pair("command", strCommand));
    retData.push_back(Pair("error_id", nError));
    retData.push_back(Pair("error_message", strErrorMessage));
    retData.push_back(Pair("data", objFile));

    Object ret;
    ret.push_back(Pair("object", "dapi_result"));
    ret.push_back(Pair("data", retData));

    return ret;
}

Object GetMessageObject(std::string strID, std::string strFromUserID, std::string strToUserID, std::string strSubCommand, std::string strMessage)
{
    Object retData;
    retData.push_back(Pair("id", strID));
    retData.push_back(Pair("command", "send_message"));
    retData.push_back(Pair("error_id", nError));
    retData.push_back(Pair("error_message", strErrorMessage));
    retData.push_back(Pair("from_uid", strFromUserID));
    retData.push_back(Pair("to_uid", strToUserID));
    retData.push_back(Pair("sub_command", strSubCommand));
    retData.push_back(Pair("payload", strMessage));

    Object ret;
    ret.push_back(Pair("object", "dapi_message"));
    ret.push_back(Pair("data", retData));

    return ret;
}

std::string SerializeJsonFromObject(Object objToSerialize)
{   
    //TODO: this is terrible, we need to correctly clean and escape the json :) 
    std::stringstream ss;
    json_spirit::write( objToSerialize, ss );
    std::string strJson = ss.str(); // escapeJsonString("'" + ss.str() + "'");
    // strJson.replace(0,1,"\"");
    // strJson.replace(strJson.size()-1,1,"\"");
    return strJson;
}

// GET OBJECT AND CONVERT TO VARIABLE

bool FindValueAsString(Object obj, std::string strName, std::string& strOut, bool fRequired=true)
{
    const Value& result = json_spirit::find_value(obj, strName);
    if (result.type() == null_type)
    {
        if(fRequired) SetError(1008, "Missing required field : " + strName);
        return false;
    }

    strOut = result.get_str();
    return true;
}

bool FindValueAsObject(Object obj, std::string strName, Object& objOut)
{
    const Value& result = json_spirit::find_value(obj, strName);
    if (result.type() == null_type)
    {
        SetError(1008, "Missing required field : " + strName);
        return false;
    }

    objOut = result.get_obj();
    return true;
}

bool FindValueAsArray(Object obj, std::string strName, Array& arrOut)
{
    const Value& result = json_spirit::find_value(obj, strName);
    if (result.type() == null_type)
    {
        SetError(1008, "Missing required field : " + strName);
        return false;
    }

    arrOut = result.get_array();
    return true;
}

bool FindValueAsInt(Object obj, std::string strName, int& nOut)
{
    const Value& result = json_spirit::find_value(obj, strName);
    if (result.type() == null_type)
    {
        SetError(1008, "Missing required field : " + strName);
        return false;
    }

    nOut = result.get_int();
    return true;
}

// DAPI FUNCTIONALITY

bool CDAPI::Execute(Object& obj)
{
    ResetErrorStatus();

    if(obj.size() > 2000)
    {
        SetError(1013, "[\"object\"] is too large");   
    }

    // dapi command only
    std::string strObject;
    if(!FindValueAsString(obj, "object", strObject))
    {
        SetError(1009, "[\"object\"] is required in data structure");
    }

    if(strObject != "dapi_command")
    {
        SetError(1010, "[\"object\"] type of \"dapi_command\" is only supported in data structure");
    }

    // required for all DAPI messages
    Object objData;
    string strCommand = "";
    if(!FindValueAsObject(obj, "data", objData))
    {
        SetError(1011, "[\"data\"] is required in data structure");
    }
    if(!FindValueAsString(objData, "command", strCommand))
    {
        SetError(1012, "[\"data\"][\"command\"] is required in data structure");
    }

    // check and make sure the usernames are OK
    ValidateUsernames(obj);

    printf("%d\n", nError);

    // SUPPORTED COMMANDS
    if(nError == 0)
    {
        if(strCommand == "get_profile") {
            if(GetProfile(obj)) return true;
        } else if (strCommand == "set_profile") {
            if(SetProfile(obj)) return true;
        } else if (strCommand == "set_private_data") {
            if(SetPrivateData(obj)) return true;
        } else if (strCommand == "get_private_data") {
            if(GetPrivateData(obj)) return true;
        } else if (strCommand == "send_message") {
            if(SendMessage(obj)) return true;
        } else if (strCommand == "broadcast_message") {
            if(BroadcastMessage(obj)) return true;
        } else if (strCommand == "invite_user") {
            if(InviteUser(obj)) return true;
        } else if (strCommand == "validate_account") {
            if(ValidateAccount(obj)) return true;
        } else if (strCommand == "search_users") {
            if(SearchUsers(obj)) return true;
        }
    }

    // UNKNOWN COMMANDS
    string strID = "";
    if(!FindValueAsString(objData, "id", strID)) return false;

    // send the user back the results of the query
    if(nError == 0) SetError(1007, "Unknown Command : " + strCommand);
    Object result;
    Object ret = GetResultObject(strID, strCommand, result);
    std::string strJson = SerializeJsonFromObject(ret);

    EventNotify(strJson);

    return false;
}

bool CDAPI::ValidateSignature(Object& obj)
{
    /*
        lookup pubkey for user
        remove signature
        hash object
        check signature against hash
    */

    return true;
}


bool CDAPI::ValidateUsernames(Object& obj)
{

    // get the user we want to open
    Object objData;
    string strUID = "";
    if(!FindValueAsObject(obj, "data", objData)) return false;

    if(FindValueAsString(objData, "to_uid", strUID, false))
    {
        if (!IsValidUsername(strUID))
        {
            SetError(1011, "Invalid to_uid - must be alphanumeric: " + strUID);
        }
    }
    if(FindValueAsString(objData, "from_uid", strUID, false))
    {
        if (!IsValidUsername(strUID))
        {
            SetError(1011, "Invalid from_uid - must be alphanumeric: " + strUID);
        }
    }

    return true;
}

bool CDAPI::GetProfile(Object& obj)
{
    /*
        {   
            "object" : "dapi_command",
            "data" : {
                “command” : ”get_profile”,
                “from_uid” : INT64,
                “to_uid” : INT64, 
                “signature” : ‘’,
                “fields” : [“fname”, “lname”]
            }
        }
    */

    // get the user we want to open
    Object objData;
    string strUID = "";
    if(!FindValueAsObject(obj, "data", objData)) return false;
    if(!FindValueAsString(objData, "to_uid", strUID)) return false;
    string strID = "";
    if(!FindValueAsString(objData, "id", strID)) return false;

    // open the file and read it
    CDriveFile file(GetProfileFile(strUID));
    if(!file.Exists()) 
    {
        SetError(1001, "File doesn't exist : " + strUID);
        return false;
    }
    file.Read();

    // send the user back the results of the query
    Object ret = GetResultObject(strID, "get_profile", file.obj);
    std::string strJson2 = SerializeJsonFromObject(file.obj);    
    std::string strJson = SerializeJsonFromObject(ret);
    EventNotify(strJson);

    return true;
}

bool CDAPI::SetProfile(Object& obj)
{
    /*
    { 
        "object" : "dapi_command",
        "data" : {
            "command": "set_profile",
            "from_uid": INT64,
            "to_uid": INT64, 
            "signature": "",
            "update" : [
                {"field":"name","value":"newvalue"}
            ]
        }
    }

    */

    std::string strObject = "";
    if(!FindValueAsString(obj, "object", strObject)) return false;

    // get the user we want to open
    Object objData;
    Array arrDataUpdate;
    string strUID = "";
    if(!FindValueAsObject(obj, "data", objData)) return false;
    if(!FindValueAsArray(objData, "update", arrDataUpdate)) return false;
    if(!FindValueAsString(objData, "to_uid", strUID)) return false;
    string strID = "";
    if(!FindValueAsString(objData, "id", strID)) return false;

    // open the file and read it
    CDriveFile file(GetProfileFile(strUID));
    if(!file.Exists())
    {
        SetError(1002, "File doesn't exist : " + strUID);
        return false;
    }
    file.Read();

    std::map<std::string, Value> mapObj;
    json_spirit::obj_to_map(file.obj, mapObj);

    for( unsigned int i = 0; i < arrDataUpdate.size(); ++i )
    {
        Object tmp = arrDataUpdate[i].get_obj();
        string strField = ""; if(!FindValueAsString(tmp, "field", strField)) return false;
        string strValue = ""; if(!FindValueAsString(tmp, "value", strValue)) return false;

        // string& strUpdate = json_spirit::find_value(obj, strField).get_str();
        // strUpdate = strValue;

        //update the users file, NOTE: this is completely insecure for the prototype (see the paper for security model!)
        //file.obj[strField] = strValue;
        mapObj[strField] = strValue;
    }

    json_spirit::map_to_obj(mapObj, file.obj);
    file.Write();

    Object ret = GetResultObject(strID, "set_profile", file.obj);
    std::string strJson = SerializeJsonFromObject(ret);

    EventNotify(strJson);

    return true;
}

bool CDAPI::GetPrivateData(Object& obj)
{
    /*
    {
        "object" : "dapi_command",
        "data" : {
            "command" = "get_private_data",
            "from_uid" = UID,
            "to_uid" = UID, 
            "signature" = ‘’,
            "slot" = 1
        }
    }
    */

    // get the user we want to open
    Object objData;
    string strUID = "";
    int nSlot = -1;
    if(!FindValueAsObject(obj, "data", objData)) return false;
    if(!FindValueAsString(objData, "to_uid", strUID)) return false;
    if(!FindValueAsInt(objData, "slot", nSlot)) return false;
    if(nSlot < 1 || nSlot > 10)
    {
        SetError(1002, "Slot out of range");
        return false;
    }
    string strID = "";
    if(!FindValueAsString(objData, "id", strID)) return false;

    // open the file and read it
    CDriveFile file(GetPrivateDataFile(strUID, nSlot));
    if(!file.Exists()) return false;
    file.Read();

    // send the user back the results of the query
    Object ret = GetResultObject(strID, "get_private_data", file.obj);
    std::string strJson = SerializeJsonFromObject(ret);
    EventNotify(strJson);

    return true;
}

bool CDAPI::SetPrivateData(Object& obj)
{
    /*
    {
        "object" : "dapi_command",
        "data" : {
            "command" : "set_private_data",
            "from_uid" : INT64,
            "to_uid" : INT64, 
            "signature" : "SIGNATURE",
            "slot" : 1,
            "payload" : JSON_WEB_ENCRYPTION
        }
    }
    */

    // get the user we want to open
    Object objData;
    string strUID = "";
    int nSlot = -1;
    if(!FindValueAsObject(obj, "data", objData)) return false;
    if(!FindValueAsString(objData, "to_uid", strUID)) return false;
    if(!FindValueAsInt(objData, "slot", nSlot)) return false;
    if(nSlot < 1 || nSlot > 10) 
    {
        SetError(1002, "Slot out of range");
        return false;
    }
    string strID = "";
    if(!FindValueAsString(objData, "id", strID)) return false;

    // open the file and read it
    CDriveFile file(GetPrivateDataFile(strUID, nSlot));
    if(!file.Exists())
    {
        Object newObj;
        newObj.push_back(Pair("access_times", 0));
        newObj.push_back(Pair("last_access", 0));
        newObj.push_back(Pair("payload", ""));
        file.obj = newObj;   
    }
    file.Read();

    //update from new payload
    std::map<std::string, Value> mapObj;
    json_spirit::obj_to_map(file.obj, mapObj);

    string strPayload = "";
    if(!FindValueAsString(objData, "payload", strPayload)) return false;
    mapObj["payload"] = strPayload;

    json_spirit::map_to_obj(mapObj, file.obj);
    file.Write();

    // send the user back the results of the query
    Object ret = GetResultObject(strID, "set_private_data", file.obj);
    std::string strJson = SerializeJsonFromObject(ret);

    EventNotify(strJson);

    return true;
    
}

// send message from one user to another through T2
bool CDAPI::SendMessage(Object& obj)
{
    /*
    { 
        "object" : "dapi_command",
        "data" : {
            "command" = "send_message",
            "sub_command" = "(addr,cmd2,cmd3)",
            "from_uid" = UID,
            "to_uid" = UID, 
            "signature" = ‘’,
            "payload" = ENCRYPTED
        }
    }
    */

    // get the user we want to open
    Object objData;
    string strUID1 = "";
    string strUID2 = "";
    string strSubCommand = "";
    string strPayload = "";
    if(!FindValueAsObject(obj, "data", objData)) return false;
    if(!FindValueAsString(objData, "from_uid", strUID1)) return false;
    if(!FindValueAsString(objData, "to_uid", strUID2)) return false;
    if(!FindValueAsString(objData, "sub_command", strSubCommand)) return false;
    if(!FindValueAsString(objData, "payload", strPayload)) return false;
    string strID = "";
    if(!FindValueAsString(objData, "id", strID)) return false;

    //TODO: this is presently sending the message to all users on the server
    Object ret = GetMessageObject(strID, strUID1, strUID2, strSubCommand, strPayload);
    std::string strJson = SerializeJsonFromObject(ret);

    EventNotify(strJson);
    return true;
}

// broadcast any message on the network from T3
bool CDAPI::BroadcastMessage(Object& obj)
{
    /*
    { 
        "object" : "dapi_command",
        "data" : {
            "command" = "broadcast",
            "sub_command" = "tx", //can support multiple message commands
            "from_uid" = UID,
            "to_uid" = UID, 
            "signature" = ‘’,
            "payload" = SERIALIZED_BASE64_ENCODED
        }
    }
    */

    // get the user we want to open
    Object objData;
    string strSubCommand = "";
    string strPayload = "";
    if(!FindValueAsObject(obj, "data", objData)) return false;
    if(!FindValueAsString(objData, "sub_command", strSubCommand)) return false;
    if(!FindValueAsString(objData, "payload", strPayload)) return false;

    string strID = "";
    if(!FindValueAsString(objData, "id", strID)) return false;

    if(strSubCommand == "tx")
    {
        CTransaction tx;
        if (!DecodeHexTx(tx, strPayload))
        {
            SetError(1003, "TX Decoding Failed");
            return false;
        }
        RelayTransaction(tx);

        Object retTx;
        retTx.push_back(Pair("tx-id", tx.GetHash().ToString()));
        //should probably figure out if it was broadcasted successfully

        // send the user back the results of the query
        Object ret = GetResultObject(strID, "broadcast_message", retTx);
        std::string strJson = SerializeJsonFromObject(ret);

        return true;

    } else {
        return false;
    }

    return false;
}


// Create new user account
bool CDAPI::InviteUser(Object& obj)
{
    // //SEND FRIEND REQUEST
    // {
    //     "object" : "dapi_command",
    //     "data" : {
    //         "command" : “invite_user”,
    //         "from_uid" : UID,
    //         "to_uid" : ENTERED_USERNAME,
    //         "to_name" : ENTERED_NAME,
    //         "to_email" : ENTERED_EMAIL, 
    //         "to_pubkey" : ENTERED_PUBKEY, 
    //         "signature" = “”
    //     }
    // }

    // get the user we want to open
    Object objData;
    string strUID = "";
    if(!FindValueAsObject(obj, "data", objData)) return false;
    if(!FindValueAsString(objData, "to_uid", strUID)) return false;

    string strID = "";
    if(!FindValueAsString(objData, "id", strID)) return false;

    // open the file and read it
    CDriveFile file(GetProfileFile(strUID));
    if(file.Exists()) 
    {
        SetError(1004, "User already exists : " + strUID);
        return false;
    }

    string strName = "";
    string strEmail = "";
    string strPubkey = "";
    if(!FindValueAsString(objData, "to_name", strName)) return false;
    if(!FindValueAsString(objData, "to_email", strEmail)) return false;
    //if(!FindValueAsString(objData, "to_pubkey", strPubkey)) return false;

    // CBitcoinAddress address(strPubkey);
    // bool isValid = address.IsValid();

    // if(!isValid)
    // {
    //     SetError(1005, "Invalid pubkey: " + strPubkey);
    //     return false;   
    // }

    Object newObj;
    newObj.push_back(Pair("status", 1));
    newObj.push_back(Pair("username", strUID));
    newObj.push_back(Pair("name", strName));
    newObj.push_back(Pair("email", strEmail));
    newObj.push_back(Pair("stars", 5));
    Array addresses;
    newObj.push_back(Pair("addresses", addresses));
    //newObj.push_back(Pair("pubkey", strPubkey));
    newObj.push_back(Pair("challenge_code", boost::lexical_cast<std::string>(GetRand(999999))));
    file.obj = newObj;   

    file.Write();

    // //RESULTING JSON
    // {
    //     "object" : "dapi_result",
    //     "data" : {
    //         "command" : “invite_user”,
    //         "from_uid" : UID,
    //         "to_uid" : ENTERED_USERNAME,
    //         "to_email" : ENTERED_EMAIL, 
    //         "to_pubkey" : ENTERED_PUBKEY, 
    //         "to_challenge_code" : RANDOMLY_GENERATED, 
    //         "signature" : “”
    //     }
    // }

    // send the user back the results of the query
    Object ret = GetResultObject(strID, "invite_user", file.obj);
    std::string strJson = SerializeJsonFromObject(ret);
    EventNotify(strJson);

    return true;
}

bool CDAPI::ValidateAccount(Object& obj)
{
    /*
        //VALIDATE ACCOUNT
        {
            "object" : "dapi_command",
            "data" : {
                "command" : “validate_account”,
                "from_uid" : UID,
                "to_uid" : ENTERED_USERNAME,
                "to_challenge_code" : RANDOMLY_GENERATED, 
                "signature" : “”
            }
        }
    */

    std::string strObject = "";
    Object objData;
    string strUID = "";
    string strChallengeCode = "";
    if(!FindValueAsString(obj, "object", strObject)) return false;

    // get the user we want to open
    if(!FindValueAsObject(obj, "data", objData)) return false;
    if(!FindValueAsString(objData, "to_uid", strUID)) return false;
    if(!FindValueAsString(objData, "to_challenge_code", strChallengeCode)) return false;

    string strID = "";
    if(!FindValueAsString(objData, "id", strID)) return false;

    // open the file and read it
    CDriveFile file(GetProfileFile(strUID));
    if(!file.Exists()) 
    {
        SetError(1001, "File doesn't exist : " + strUID);
        return false;
    }
    file.Read();

    std::map<std::string, Value> mapObj;
    json_spirit::obj_to_map(file.obj, mapObj);

    if(mapObj["status"].get_int() != 1)
    {
        SetError(1008, "Account in wrong state to be validated");
        return false;
    }

    if(mapObj["challenge_code"].get_str() == strChallengeCode)
    {
        mapObj["challenge_code"] = "";
        mapObj["status"] = 2;
    } else {
        SetError(1006, "Invalid challenge_code : " + strChallengeCode);
        return false;
    }
    
    json_spirit::map_to_obj(mapObj, file.obj);
    file.Write();

    /*
        //VALIDATE_ACCOUNT
        {
            "object" : "dapi_result",
            "data" : {
                "command" : “validate_account”,
                "from_uid" : UID,
                "to_uid" : ENTERED_USERNAME, 
                "signature" : “”,
                “error_id” : 1000,
                “error_description” : “none” 
            }
        }
    */

    // send the user back the results of the query
    Object ret = GetResultObject(strID, "“validate_account”", file.obj);
    std::string strJson = SerializeJsonFromObject(ret);
    EventNotify(strJson);

    return true;
}


bool CDAPI::SearchUsers(Object& obj)
{
    /*
        //SEARCH USERS
        {
            "object" : "dapi_command",
            "data" : {
                "command" : "search_users",
                "match_fields" : ["fiat_converter" : 1],
                "start" : 0,
                "limit" : 10
            }
        }
    */

    Object objData;
    string strID = "";
    if(!FindValueAsObject(obj, "data", objData)) return false;
    if(!FindValueAsString(objData, "id", strID)) return false;


    // open the file and read it
    CDriveFile file(GetIndexFile("fiat_converters.js"));

    if(!file.Exists()) 
    {
        SetError(1003, "File doesn't exist : " + GetIndexFile("fiat_converters.js"));
        return false;
    }
    file.Read();

    // send the user back the results of the query
    Object ret = GetResultObject(strID, "search_users", file.obj);
    std::string strJson = SerializeJsonFromObject(ret);

    EventNotify(strJson);

    return true;
}

