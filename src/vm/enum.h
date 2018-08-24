#ifndef ENUM_H
#define ENUM_H

#define DEFINE_ENUM(NAME, ENUMX) typedef enum NAME { ENUMX(ENUM_ENTRY) } NAME
#define ENUM_ENTRY(ENTRY) ENTRY,

#define DECLARE_TO_STRING(ENUM_NAME) extern const char* CONCAT(ENUM_NAME, Name)[]

#define DEFINE_TO_STRING(ENUM_NAME, ENUMX) \
	const char* CONCAT(ENUM_NAME, Name)[] = { \
			ENUMX(STRINGIFY) \
	}

#define CONCAT(X, Y) X##Y
#define STRINGIFY(X) #X,

#endif
