/*
 *
 *  Copyright (c) 2015, Red Hat, Inc.
 *  Copyright (c) 2015, Masatake YAMATO
 *
 *  Author: Masatake YAMATO <yamato@redhat.com>
 *
 *   This source code is released for free distribution under the terms of the
 *   GNU General Public License version 2 or (at your option) any later version.
 *
 */

#include "general.h"  /* must always come first */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "ctags.h"
#include "debug.h"
#include "entry.h"
#include "field.h"
#include "kind.h"
#include "options.h"
#include "read.h"
#include "routines.h"


typedef struct sFieldObject {
	fieldDefinition *def;
	unsigned int fixed:   1;   /* fields which cannot be disabled. */
	vString     *buffer;
	const char* nameWithPrefix;
	langType language;
	fieldType sibling;
} fieldObject;

static const char *renderFieldName (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldNameNoEscape (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
											bool *rejected);
static const char *renderFieldInput (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldInputNoEscape (const tagEntryInfo *const tag, const char *value, vString* b,
											 bool *rejected);
static const char *renderFieldCompactInputLine (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldSignature (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldScope (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldScopeNoEscape (const tagEntryInfo *const tag, const char *value, vString* b,
											 bool *rejected);
static const char *renderFieldTyperef (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldInherits (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldKindName (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldLineNumber (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldLanguage (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldAccess (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldKindLetter (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldImplementation (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldFile (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldPattern (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldRole (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldRefMarker (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldExtras (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldXpath (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldScopeKindName(const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);
static const char *renderFieldEnd (const tagEntryInfo *const tag, const char *value, vString* b, bool *rejected);

static bool     isLanguageFieldAvailable  (const tagEntryInfo *const tag);
static bool     isTyperefFieldAvailable   (const tagEntryInfo *const tag);
static bool     isFileFieldAvailable      (const tagEntryInfo *const tag);
static bool     isInheritsFieldAvailable  (const tagEntryInfo *const tag);
static bool     isAccessFieldAvailable    (const tagEntryInfo *const tag);
static bool     isImplementationFieldAvailable (const tagEntryInfo *const tag);
static bool     isSignatureFieldAvailable (const tagEntryInfo *const tag);
static bool     isRoleFieldAvailable      (const tagEntryInfo *const tag);
static bool     isExtrasFieldAvailable    (const tagEntryInfo *const tag);
static bool     isXpathFieldAvailable     (const tagEntryInfo *const tag);
static bool     isEndFieldAvailable       (const tagEntryInfo *const tag);


#define DEFINE_FIELD(L, N, V, H, DT, ...)				\
	DEFINE_FIELD_FULL (L, N, V, H, NULL, DT, __VA_ARGS__)
#define DEFINE_FIELD_FULL(L, N, V, H, A, DT, ...)	\
	{					\
		.letter        = L,		\
		.name          = N,		\
		.description   = H,		\
		.enabled       = V,		\
		.renderEscaped = { __VA_ARGS__ },		\
		.isValueAvailable = A,		\
		.dataType = DT, \
	}

#define WITH_DEFUALT_VALUE(str) ((str)?(str):"-")

static fieldDefinition fieldDefinitionsFixed [] = {
        /* FIXED FIELDS */
	DEFINE_FIELD ('N', "name",     true,
			  "tag name (fixed field)",
			  FIELDTYPE_STRING,
			  [WRITER_U_CTAGS] = renderFieldName,
			  [WRITER_E_CTAGS] = renderFieldNameNoEscape,
			  [WRITER_JSON]    = renderFieldNameNoEscape,
			  ),
	DEFINE_FIELD ('F', "input",    true,
			   "input file (fixed field)",
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldInput,
			   [WRITER_E_CTAGS] = renderFieldInputNoEscape,
			   [WRITER_JSON]    = renderFieldInputNoEscape,
		),
	DEFINE_FIELD ('P', "pattern",  true,
			   "pattern (fixed field)",
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldPattern),
};

static fieldDefinition fieldDefinitionsExuberant [] = {
	DEFINE_FIELD ('C', "compact",        false,
			   "compact input line (fixed field, only used in -x option)",
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldCompactInputLine),

	/* EXTENSION FIELDS */
	DEFINE_FIELD_FULL ('a', "access",         false,
		      "Access (or export) of class members",
			  isAccessFieldAvailable,
			  FIELDTYPE_STRING,
		      [WRITER_U_CTAGS] = renderFieldAccess),
	DEFINE_FIELD_FULL ('f', "file",           true,
		      "File-restricted scoping",
			  isFileFieldAvailable,
			  FIELDTYPE_BOOL,
		      [WRITER_U_CTAGS] = renderFieldFile),
	DEFINE_FIELD_FULL ('i', "inherits",       false,
		      "Inheritance information",
			  isInheritsFieldAvailable,
			  FIELDTYPE_STRING|FIELDTYPE_BOOL,
		      [WRITER_U_CTAGS] = renderFieldInherits),
	DEFINE_FIELD ('K', NULL,             false,
		      "Kind of tag as full name",
		      FIELDTYPE_STRING,
		      [WRITER_U_CTAGS] = renderFieldKindName),
	DEFINE_FIELD ('k', NULL,             true,
			   "Kind of tag as a single letter",
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldKindLetter),
	DEFINE_FIELD_FULL ('l', "language",       false,
			   "Language of input file containing tag",
			   isLanguageFieldAvailable,
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldLanguage),
	DEFINE_FIELD_FULL ('m', "implementation", false,
			   "Implementation information",
			   isImplementationFieldAvailable,
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldImplementation),
	DEFINE_FIELD ('n', "line",           false,
			   "Line number of tag definition",
			   FIELDTYPE_INTEGER,
			   [WRITER_U_CTAGS] = renderFieldLineNumber),
	DEFINE_FIELD_FULL ('S', "signature",	     false,
			   "Signature of routine (e.g. prototype or parameter list)",
			   isSignatureFieldAvailable,
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldSignature),
	DEFINE_FIELD ('s', NULL,             true,
			   "Scope of tag definition (`p' can be used for printing its kind)",
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldScope,
			   [WRITER_E_CTAGS] = renderFieldScopeNoEscape,
			   [WRITER_JSON]    = renderFieldScopeNoEscape),
	DEFINE_FIELD_FULL ('t', "typeref",        true,
			   "Type and name of a variable or typedef",
			   isTyperefFieldAvailable,
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldTyperef),
	DEFINE_FIELD ('z', "kind",           false,
			   "Include the \"kind:\" key in kind field (use k or K) in tags output, kind full name in xref output",
			   FIELDTYPE_STRING,
			   /* Following renderer is for handling --_xformat=%{kind};
			      and is not for tags output. */
			   [WRITER_U_CTAGS] = renderFieldKindName),
};

static fieldDefinition fieldDefinitionsUniversal [] = {
	DEFINE_FIELD_FULL ('r', "role",    false,
			   "Role",
			   isRoleFieldAvailable,
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldRole),
	DEFINE_FIELD ('R',  NULL,     false,
			   "Marker (R or D) representing whether tag is definition or reference",
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldRefMarker),
	DEFINE_FIELD ('Z', "scope",   false,
			  "Include the \"scope:\" key in scope field (use s) in tags output, scope name in xref output",
			   FIELDTYPE_STRING,
			   /* Following renderer is for handling --_xformat=%{scope};
			      and is not for tags output. */
			   [WRITER_U_CTAGS] = renderFieldScope,
			   [WRITER_E_CTAGS] = renderFieldScopeNoEscape,
			   [WRITER_JSON]    = renderFieldScopeNoEscape),
	DEFINE_FIELD_FULL ('E', "extras",   false,
			   "Extra tag type information",
			   isExtrasFieldAvailable,
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldExtras),
	DEFINE_FIELD_FULL ('x', "xpath",   false,
			   "xpath for the tag",
			   isXpathFieldAvailable,
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldXpath),
	DEFINE_FIELD ('p', "scopeKind", false,
			   "Kind of scope as full name",
			   FIELDTYPE_STRING,
			   [WRITER_U_CTAGS] = renderFieldScopeKindName),
	DEFINE_FIELD_FULL ('e', "end", false,
			   "end lines of various items",
			   isEndFieldAvailable,
			   FIELDTYPE_INTEGER,
			   [WRITER_U_CTAGS] = renderFieldEnd),
};


static unsigned int       fieldObjectUsed = 0;
static unsigned int       fieldObjectAllocated = 0;
static fieldObject* fieldObjects = NULL;

extern void initFieldObjects (void)
{
	int i;
	fieldObject *fobj;

	Assert (fieldObjects == NULL);

	fieldObjectAllocated
	  = ARRAY_SIZE (fieldDefinitionsFixed)
	  + ARRAY_SIZE (fieldDefinitionsExuberant)
	  + ARRAY_SIZE (fieldDefinitionsUniversal);
	fieldObjects = xMalloc (fieldObjectAllocated, fieldObject);

	fieldObjectUsed = 0;

	for (i = 0; i < ARRAY_SIZE (fieldDefinitionsFixed); i++)
	{
		fobj = fieldObjects + i + fieldObjectUsed;
		fobj->def = fieldDefinitionsFixed + i;
		fobj->fixed  = 1;
		fobj->buffer = NULL;
		fobj->nameWithPrefix = fobj->def->name;
		fobj->language = LANG_IGNORE;
		fobj->sibling  = FIELD_UNKNOWN;
	}
	fieldObjectUsed += ARRAY_SIZE (fieldDefinitionsFixed);

	for (i = 0; i < ARRAY_SIZE (fieldDefinitionsExuberant); i++)
	{
		fobj = fieldObjects + i + fieldObjectUsed;
		fobj->def = fieldDefinitionsExuberant +i;
		fobj->fixed = 0;
		fobj->buffer = NULL;
		fobj->nameWithPrefix = fobj->def->name;
		fobj->language = LANG_IGNORE;
		fobj->sibling  = FIELD_UNKNOWN;
	}
	fieldObjectUsed += ARRAY_SIZE (fieldDefinitionsExuberant);

	for (i = 0; i < ARRAY_SIZE (fieldDefinitionsUniversal); i++)
	{
		char *nameWithPrefix;

		fobj = fieldObjects + i + fieldObjectUsed;
		fobj->def = fieldDefinitionsUniversal + i;
		fobj->fixed = 0;
		fobj->buffer = NULL;

		if (fobj->def->name)
		{
			nameWithPrefix = eMalloc (sizeof CTAGS_FIELD_PREFIX + strlen (fobj->def->name) + 1);
			nameWithPrefix [0] = '\0';
			strcat (nameWithPrefix, CTAGS_FIELD_PREFIX);
			strcat (nameWithPrefix, fobj->def->name);
			fobj->nameWithPrefix = nameWithPrefix;
		}
		else
			fobj->nameWithPrefix = NULL;
		fobj->language = LANG_IGNORE;
		fobj->sibling  = FIELD_UNKNOWN;
	}
	fieldObjectUsed += ARRAY_SIZE (fieldDefinitionsUniversal);

	Assert ( fieldObjectAllocated == fieldObjectUsed );
}

static fieldObject* getFieldObject(fieldType type)
{
	Assert ((0 <= type) && (type < fieldObjectUsed));
	return fieldObjects + type;
}

extern fieldType getFieldTypeForOption (char letter)
{
	unsigned int i;

	for (i = 0; i < fieldObjectUsed; i++)
	{
		if (fieldObjects [i].def->letter == letter)
			return i;
	}
	return FIELD_UNKNOWN;
}

extern fieldType getFieldTypeForName (const char *name)
{
	return getFieldTypeForNameAndLanguage (name, LANG_IGNORE);
}

extern fieldType getFieldTypeForNameAndLanguage (const char *fieldName, langType language)
{
	static bool initialized = false;
	unsigned int i;

	if (fieldName == NULL)
		return FIELD_UNKNOWN;

	if (language == LANG_AUTO && (initialized == false))
	{
		initialized = true;
		initializeParser (LANG_AUTO);
	}
	else if (language != LANG_IGNORE && (initialized == false))
		initializeParser (language);

	for (i = 0; i < fieldObjectUsed; i++)
	{
		if (fieldObjects [i].def->name
		    && strcmp (fieldObjects [i].def->name, fieldName) == 0
		    && ((language == LANG_AUTO)
			|| (fieldObjects [i].language == language)))
			return i;
	}

	return FIELD_UNKNOWN;
}

extern const char* getFieldName(fieldType type)
{
	fieldObject* fobj;

	fobj = getFieldObject (type);
	if (Option.putFieldPrefix)
		return fobj->nameWithPrefix;
	else
		return fobj->def->name;
}

extern bool doesFieldHaveValue (fieldType type, const tagEntryInfo *tag)
{
	if (getFieldObject(type)->def->isValueAvailable)
		return getFieldObject(type)->def->isValueAvailable(tag);
	else
		return true;
}

#define PR_FIELD_WIDTH_LETTER     7
#define PR_FIELD_WIDTH_NAME      15
#define PR_FIELD_WIDTH_LANGUAGE  16
#define PR_FIELD_WIDTH_DESC      30
#define PR_FIELD_WIDTH_XFMT      6
#define PR_FIELD_WIDTH_JSTYPE    6
#define PR_FIELD_WIDTH_ENABLED   7

#define PR_FIELD_STR(X) PR_FIELD_WIDTH_##X
#define PR_FIELD_FMT(X,T) "%-" STRINGIFY(PR_FIELD_STR(X)) STRINGIFY(T)

#define MAKE_FIELD_FMT(LETTER_SPEC)		\
	PR_FIELD_FMT (LETTER,LETTER_SPEC)	\
	" "					\
	PR_FIELD_FMT (NAME,s)			\
	" "					\
	PR_FIELD_FMT (ENABLED,s)		\
	" "					\
	PR_FIELD_FMT (LANGUAGE,s)		\
	" "					\
	PR_FIELD_FMT (XFMT,s)		\
	" "					\
	PR_FIELD_FMT (JSTYPE,s)		\
	" "					\
	PR_FIELD_FMT (DESC,s)			\
	"\n"

static void printField (fieldType i)
{
	unsigned char letter = fieldObjects[i].def->letter;
	const char *name;
	const char *language;
	char  typefields [] = "---";

	if (letter == NUL_FIELD_LETTER)
		letter = '-';

	if (! fieldObjects[i].def->name)
		name = "NONE";
	else
		name = getFieldName (i);

	if (fieldObjects[i].language == LANG_IGNORE)
		language = "NONE";
	else
		language = getLanguageName (fieldObjects[i].language);

	{
		unsigned int bmask, offset;
		unsigned int dt = getFieldDataType(i);
		for (bmask = 1, offset = 0;
			 bmask < FIELDTYPE_END_MARKER;
			 bmask <<= 1, offset++)
			if (dt & bmask)
				typefields[offset] = fieldDataTypeFalgs[offset];
	}

	printf((Option.machinable? "%c\t%s\t%s\t%s\t%s\t%s\t%s\n": MAKE_FIELD_FMT(c)),
	       letter,
	       name,
	       isFieldEnabled (i)? "on": "off",
	       language,
	       getFieldObject (i)->def->renderEscaped? "TRUE": "FALSE",
		   typefields,
	       fieldObjects[i].def->description? fieldObjects[i].def->description: "NONE");
}

extern void printFields (int language)
{
	unsigned int i;

	if (Option.withListHeader)
		printf ((Option.machinable? "%s\t%s\t%s\t%s\t%s\t%s\t%s\n": MAKE_FIELD_FMT(s)),
			"#LETTER", "NAME", "ENABLED", "LANGUAGE", "XFMT", "JSTYPE", "DESCRIPTION");

	for (i = 0; i < fieldObjectUsed; i++)
	{
		if (language == LANG_AUTO || getFieldOwner (i) == language)
			printField (i);
	}
}

static const char *renderAsIs (vString* b CTAGS_ATTR_UNUSED, const char *s)
{
	return s;
}

static const char *renderEscapedString (const char *s,
					const tagEntryInfo *const tag CTAGS_ATTR_UNUSED,
					vString* b)
{
	vStringCatSWithEscaping (b, s);
	return vStringValue (b);
}

static const char *renderEscapedName (const char* s,
				      const tagEntryInfo *const tag,
				      vString* b)
{
	const char* base = s;

	for (; *s; s++)
	{
		int c = *s;
		if ((c > 0x00 && c <= 0x1F) || c == 0x7F)
		{
			verbose ("Unexpected character (0 < *c && *c < 0x20) included in a tagEntryInfo: %s\n", base);
			verbose ("File: %s, Line: %lu, Lang: %s, Kind: %c\n",
				 tag->inputFileName, tag->lineNumber, tag->language, tag->kind->letter);
			verbose ("Escape the character\n");
			break;
		}
		else if (c == '\\')
			break;
		else
			continue;
	}

	if (*s == '\0')
		return base;

	vStringNCatS (b, base, s - base);

	return renderEscapedString (s, tag, b);
}

static const char *renderFieldName (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
									bool *rejected)
{
	return renderEscapedName (tag->name, tag, b);
}

static const char *renderFieldNameNoEscape (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
											bool *rejected)
{
	if (strpbrk (tag->name, " \t"))
	{
		*rejected = true;
		return NULL;
	}
	return renderAsIs (b, tag->name);
}

static const char *renderFieldInput (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
									 bool *rejected)
{
	const char *f = tag->inputFileName;

	if (Option.lineDirectives && tag->sourceFileName)
		f = tag->sourceFileName;
	return renderEscapedString (f, tag, b);
}

static const char *renderFieldInputNoEscape (const tagEntryInfo *const tag, const char *value, vString* b,
											 bool *rejected)
{
	const char *f = tag->inputFileName;

	if (Option.lineDirectives && tag->sourceFileName)
		f = tag->sourceFileName;

	if (strpbrk (f, " \t"))
	{
		*rejected = true;
		return NULL;
	}

	return renderAsIs (b, f);
}

static const char *renderFieldSignature (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
										 bool *rejected)
{
	return renderEscapedString (WITH_DEFUALT_VALUE (tag->extensionFields.signature),
				    tag, b);
}

static const char *renderFieldScope (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
									 bool *rejected)
{
	const char* scope;

	getTagScopeInformation ((tagEntryInfo *const)tag, NULL, &scope);
	return scope? renderEscapedName (scope, tag, b): NULL;
}

static const char *renderFieldScopeNoEscape (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
											 bool *rejected)
{
	const char* scope;

	getTagScopeInformation ((tagEntryInfo *const)tag, NULL, &scope);
	if (scope && strpbrk (scope, " \t"))
	{
		*rejected = true;
		return NULL;
	}

	return scope? renderAsIs (b, scope): NULL;
}

static const char *renderFieldInherits (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
										bool *rejected)
{
	return renderEscapedString (WITH_DEFUALT_VALUE (tag->extensionFields.inheritance),
				    tag, b);
}

static const char *renderFieldTyperef (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
									   bool *rejected)
{
	return renderEscapedName (WITH_DEFUALT_VALUE (tag->extensionFields.typeRef [1]), tag, b);
}


extern const char* renderFieldEscaped (writerType writer,
				      fieldType type,
				       const tagEntryInfo *tag,
				       int index,
					   bool *rejected)
{
	fieldObject *fobj = fieldObjects + type;
	const char *value;
	renderEscaped rfn;
	bool stub;

	Assert (tag);
	Assert (fobj->def->renderEscaped);

	fobj->buffer = vStringNewOrClear (fobj->buffer);

	if (index >= 0)
	{
		Assert ( tag->usedParserFields > index );
		value = tag->parserFields[ index ].value;
	}
	else
		value = NULL;

	rfn = fobj->def->renderEscaped [writer];
	if (rfn == NULL)
		rfn = fobj->def->renderEscaped [WRITER_DEFAULT];

	if (!rejected)
		rejected = &stub;
	return rfn (tag, value, fobj->buffer, rejected);
}

/*  Writes "line", stripping leading and duplicate white space.
 */
static const char* renderCompactInputLine (vString *b,  const char *const line)
{
	bool lineStarted = false;
	const char *p;
	int c;

	/*  Write everything up to, but not including, the newline.
	 */
	for (p = line, c = *p  ;  c != NEWLINE  &&  c != '\0'  ;  c = *++p)
	{
		if (lineStarted  || ! isspace (c))  /* ignore leading spaces */
		{
			lineStarted = true;
			if (isspace (c))
			{
				int next;

				/*  Consume repeating white space.
				 */
				while (next = *(p+1) , isspace (next)  &&  next != NEWLINE)
					++p;
				c = ' ';  /* force space character for any white space */
			}
			if (c != CRETURN  ||  *(p + 1) != NEWLINE)
				vStringPut (b, c);
		}
	}
	return vStringValue (b);
}

static const char *renderFieldKindName (const tagEntryInfo *const tag, const char *value CTAGS_ATTR_UNUSED, vString* b,
										bool *rejected)
{
	return renderAsIs (b, tag->kind->name);
}

static const char *renderFieldCompactInputLine (const tagEntryInfo *const tag,
						const char *value CTAGS_ATTR_UNUSED,
						 vString* b,
						bool *rejected)
{
	const char *line;
	static vString *tmp;

	tmp = vStringNewOrClear (tmp);

	line = readLineFromBypassAnyway (tmp, tag, NULL);
	if (line)
		renderCompactInputLine (b, line);
	else
	{
		/* If no associated line for tag is found, we cannot prepare
		 * parameter to writeCompactInputLine(). In this case we
		 * use an empty string as LINE.
		 */
		vStringClear (b);
	}

	return vStringValue (b);
}

static const char *renderFieldLineNumber (const tagEntryInfo *const tag,
					  const char *value CTAGS_ATTR_UNUSED,
					  vString* b,
					  bool *rejected)
{
	long ln = tag->lineNumber;
	char buf[32] = {[0] = '\0'};

	if (Option.lineDirectives && (tag->sourceLineNumberDifference != 0))
		ln += tag->sourceLineNumberDifference;
	snprintf (buf, sizeof(buf), "%ld", ln);
	vStringCatS (b, buf);
	return vStringValue (b);
}

static const char *renderFieldRole (const tagEntryInfo *const tag,
				    const char *value CTAGS_ATTR_UNUSED,
				    vString* b,
					bool *rejected)
{
	int rindex = tag->extensionFields.roleIndex;
	const roleDesc * role;

	if (rindex == ROLE_INDEX_DEFINITION)
		vStringClear (b);
	else
	{
		Assert (rindex < tag->kind->nRoles);
		role  = & (tag->kind->roles [rindex]);
		return renderRole (role, b);
	}

	return vStringValue (b);
}

static const char *renderFieldLanguage (const tagEntryInfo *const tag,
					const char *value CTAGS_ATTR_UNUSED,
					vString* b,
					bool *rejected)
{
	const char *l = tag->language;

	if (Option.lineDirectives && tag->sourceLanguage)
		l = tag->sourceLanguage;

	return renderAsIs (b, WITH_DEFUALT_VALUE(l));
}

static const char *renderFieldAccess (const tagEntryInfo *const tag,
				      const char *value,
				      vString* b,
					  bool *rejected)
{
	return renderAsIs (b, WITH_DEFUALT_VALUE (tag->extensionFields.access));
}

static const char *renderFieldKindLetter (const tagEntryInfo *const tag,
					  const char *value CTAGS_ATTR_UNUSED,
					  vString* b,
					  bool *rejected)
{
	static char c[2] = { [1] = '\0' };

	c [0] = tag->kind->letter;

	return renderAsIs (b, c);
}

static const char *renderFieldImplementation (const tagEntryInfo *const tag,
					      const char *value CTAGS_ATTR_UNUSED,
					      vString* b,
						  bool *rejected)
{
	return renderAsIs (b, WITH_DEFUALT_VALUE (tag->extensionFields.implementation));
}

static const char *renderFieldFile (const tagEntryInfo *const tag,
				    const char *value CTAGS_ATTR_UNUSED,
				    vString* b,
					bool *rejected)
{
	return renderAsIs (b, tag->isFileScope? "file": "-");
}

static const char *renderFieldPattern (const tagEntryInfo *const tag,
				       const char *value CTAGS_ATTR_UNUSED,
				       vString* b,
					   bool *rejected)
{
	if (tag->lineNumberEntry)
		return NULL;

	if (tag->pattern)
		vStringCatS (b, tag->pattern);
	else
	{
		char* tmp;

		tmp = makePatternString (tag);
		vStringCatS (b, tmp);
		eFree (tmp);
	}
	return vStringValue (b);
}

static const char *renderFieldRefMarker (const tagEntryInfo *const tag,
					 const char *value CTAGS_ATTR_UNUSED,
					 vString* b,
					 bool *rejected)
{
	static char c[2] = { [1] = '\0' };

	c [0] = tag->extensionFields.roleIndex == ROLE_INDEX_DEFINITION? 'D': 'R';

	return renderAsIs (b, c);
}

static const char *renderFieldExtras (const tagEntryInfo *const tag,
				     const char *value CTAGS_ATTR_UNUSED,
				     vString* b,
					 bool *rejected)
{
	int i;
	bool hasExtra = false;

	for (i = 0; i < XTAG_COUNT; i++)
	{
		const char *name = getXtagName (i);

		if (!name)
			continue;

		if (isTagExtraBitMarked (tag, i))
		{

			if (hasExtra)
				vStringPut (b, ',');
			vStringCatS (b, name);
			hasExtra = true;
		}
	}

	if (hasExtra)
		return vStringValue (b);
	else
		return NULL;
}

static const char *renderFieldXpath (const tagEntryInfo *const tag,
				     const char *value,
				     vString* b,
					 bool *rejected)
{
#ifdef HAVE_LIBXML
	if (tag->extensionFields.xpath)
		return renderEscapedString (tag->extensionFields.xpath,
					    tag, b);
#endif
	return NULL;
}

static const char *renderFieldScopeKindName(const tagEntryInfo *const tag,
					    const char *value,
					    vString* b,
						bool *rejected)
{
	const char* kind;

	getTagScopeInformation ((tagEntryInfo *const)tag, &kind, NULL);
	return kind? renderAsIs (b, kind): NULL;
}

static const char *renderFieldEnd (const tagEntryInfo *const tag,
				   const char *value,
				   vString* b,
				   bool *rejected)
{
	static char buf[16];

	if (tag->extensionFields.endLine != 0)
	{
		sprintf (buf, "%lu", tag->extensionFields.endLine);
		return renderAsIs (b, buf);
	}
	else
		return NULL;
}

static bool     isLanguageFieldAvailable (const tagEntryInfo *const tag)
{
	return (tag->language != NULL)? true: false;
}

static bool     isTyperefFieldAvailable  (const tagEntryInfo *const tag)
{
	return (tag->extensionFields.typeRef [0] != NULL
		&& tag->extensionFields.typeRef [1] != NULL)? true: false;
}

static bool     isFileFieldAvailable  (const tagEntryInfo *const tag)
{
	return tag->isFileScope? true: false;
}

static bool     isInheritsFieldAvailable (const tagEntryInfo *const tag)
{
	return (tag->extensionFields.inheritance != NULL)? true: false;
}

static bool     isAccessFieldAvailable   (const tagEntryInfo *const tag)
{
	return (tag->extensionFields.access != NULL)? true: false;
}

static bool     isImplementationFieldAvailable (const tagEntryInfo *const tag)
{
	return (tag->extensionFields.implementation != NULL)? true: false;
}

static bool     isSignatureFieldAvailable (const tagEntryInfo *const tag)
{
	return (tag->extensionFields.signature != NULL)? true: false;
}

static bool     isRoleFieldAvailable      (const tagEntryInfo *const tag)
{
	return (tag->extensionFields.roleIndex != ROLE_INDEX_DEFINITION)? true: false;
}

static bool     isExtrasFieldAvailable     (const tagEntryInfo *const tag)
{
	int i;
	for (i = 0; i < sizeof (tag->extra); i++)
	{
		if (tag->extra [i])
			return true;
	}

	return false;
}

static bool     isXpathFieldAvailable      (const tagEntryInfo *const tag)
{
#ifdef HAVE_LIBXML
	return (tag->extensionFields.xpath != NULL)? true: false;
#else
	return false;
#endif
}

static bool     isEndFieldAvailable       (const tagEntryInfo *const tag)
{
	return (tag->extensionFields.endLine != 0)? true: false;
}

extern bool isFieldEnabled (fieldType type)
{
	return getFieldObject(type)->def->enabled;
}

static bool isFieldFixed (fieldType type)
{
	return getFieldObject(type)->fixed? true: false;
}

extern bool enableField (fieldType type, bool state, bool warnIfFixedField)
{
	fieldDefinition *def = getFieldObject(type)->def;
	bool old = def->enabled;
	if (isFieldFixed (type))
	{
		if ((!state) && warnIfFixedField)
		{
			if (def->name && def->letter != NUL_FIELD_LETTER)
				error(WARNING, "Cannot disable fixed field: '%c'{%s}",
				      def->letter, def->name);
			else if (def->name)
				error(WARNING, "Cannot disable fixed field: {%s}",
				      def->name);
			else if (def->letter != NUL_FIELD_LETTER)
				error(WARNING, "Cannot disable fixed field: '%c'",
				      getFieldObject(type)->def->letter);
			else
				AssertNotReached();
		}
	}
	else
	{
		getFieldObject(type)->def->enabled = state;

		if (isCommonField (type))
			verbose ("enable field \"%s\": %s\n",
				 getFieldObject(type)->def->name,
				 (state? "TRUE": "FALSE"));
		else
			verbose ("enable field \"%s\"<%s>: %s\n",
				 getFieldObject(type)->def->name,
				 getLanguageName (getFieldOwner(type)),
				 (state? "TRUE": "FALSE"));
	}
	return old;
}

extern bool isCommonField (fieldType type)
{
	return (FIELD_BUILTIN_LAST < type)? false: true;
}

extern int     getFieldOwner (fieldType type)
{
	return getFieldObject(type)->language;
}

extern unsigned int getFieldDataType (fieldType type)
{
	return getFieldObject(type)->def->dataType;
}

extern bool isFieldRenderable (fieldType type)
{
	return getFieldObject(type)->def->renderEscaped [WRITER_DEFAULT]? true: false;
}

extern int countFields (void)
{
	return fieldObjectUsed;
}

extern fieldType nextSiblingField (fieldType type)
{
	fieldObject *fobj;

	fobj = fieldObjects + type;
	return fobj->sibling;
}

static void updateSiblingField (fieldType type, const char* name)
{
	int i;
	fieldObject *fobj;

	for (i = type; i > 0; i--)
	{
		fobj = fieldObjects + i - 1;
		if (fobj->def->name && (strcmp (fobj->def->name, name) == 0))
		{
			Assert (fobj->sibling == FIELD_UNKNOWN);
			fobj->sibling = type;
			break;
		}
	}
}

static const char* defaultRenderer (const tagEntryInfo *const tag,
				    const char *value,
				    vString * buffer,
					bool *rejected)
{
	return value;
}

extern int defineField (fieldDefinition *def, langType language)
{
	fieldObject *fobj;
	char *nameWithPrefix;
	size_t i;

	Assert (def);
	Assert (def->name);
	for (i = 0; i < strlen (def->name); i++)
	{
		Assert ( isalnum (def->name [i]) );
	}
	def->letter = NUL_FIELD_LETTER;

	if (fieldObjectUsed == fieldObjectAllocated)
	{
		fieldObjectAllocated *= 2;
		fieldObjects = xRealloc (fieldObjects, fieldObjectAllocated, fieldObject);
	}
	fobj = fieldObjects + (fieldObjectUsed);
	def->ftype = fieldObjectUsed++;

	if (def->renderEscaped [WRITER_DEFAULT] == NULL)
		def->renderEscaped [WRITER_DEFAULT] = defaultRenderer;

	if (! def->dataType)
		def->dataType = FIELDTYPE_STRING;

	fobj->def = def;

	fobj->fixed =  0;
	fobj->buffer = NULL;

	nameWithPrefix = eMalloc (sizeof CTAGS_FIELD_PREFIX + strlen (def->name) + 1);
	nameWithPrefix [0] = '\0';
	strcat (nameWithPrefix, CTAGS_FIELD_PREFIX);
	strcat (nameWithPrefix, def->name);
	fobj->nameWithPrefix = nameWithPrefix;

	fobj->language = language;
	fobj->sibling  = FIELD_UNKNOWN;

	updateSiblingField (def->ftype, def->name);
	return def->ftype;
}
