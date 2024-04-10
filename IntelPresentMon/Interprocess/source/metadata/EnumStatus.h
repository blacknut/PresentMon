#pragma once
#include "../../../PresentMonAPI2/PresentMonAPI.h"

// enum annotation (enum_name_fragment, key_name_fragment, name, short_name, description)
#define ENUM_KEY_LIST_STATUS(X_) \
		X_(STATUS, SUCCESS, "Success", "", "Operation succeeded") \
		X_(STATUS, FAILURE, "Failure", "", "Operation failed") \
		X_(STATUS, SESSION_NOT_OPEN, "Session Not Open", "", "Operation requires an open session") \
		X_(STATUS, SERVICE_ERROR, "", "", "") \
		X_(STATUS, INVALID_ETL_FILE, "", "", "") \
		X_(STATUS, DATA_LOSS, "", "", "") \
		X_(STATUS, NO_DATA, "", "", "") \
		X_(STATUS, INVALID_PID, "", "", "") \
		X_(STATUS, STREAM_ALREADY_EXISTS, "", "", "") \
		X_(STATUS, UNABLE_TO_CREATE_NSM, "", "", "") \
		X_(STATUS, INVALID_ADAPTER_ID, "", "", "") \
		X_(STATUS, OUT_OF_RANGE, "", "", "") \
		X_(STATUS, INSUFFICIENT_BUFFER, "", "", "")