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

#include "general.h"

#include <stdio.h>
#include "kind.h"

extern void printRole (const roleDesc* const role)
{
	if (role)
		printf ("%s\t%s\t%s\n", role->name, role->description, role->enabled? "on": "off");
}

extern const char *renderRole (const roleDesc* const role, vString* b)
{
	vStringCatS (b, role->name);
	return vStringValue (b);
}

extern void printKind (const kindOption* const kind, boolean allKindFields, boolean indent)
{
	if (allKindFields)
	{
		printf ("%s%c\t%s\t%s\t%s\treferenceOnly:%s\tnRoles:%d\n", indent ? "\t"           : "",
			kind->letter,
			kind->name        != NULL ? kind->name        : "",
			kind->description != NULL ? kind->description : "",
			kind->enabled             ? "on"              : "off",
			kind->referenceOnly       ? "TRUE"            : "FALSE",
			kind->nRoles);
	}
	else if (!kind->referenceOnly)
	{
		printf ("%s%c  %s%s\n", indent ? "    " : "", kind->letter,
			kind->description != NULL ? kind->description :
			(kind->name != NULL ? kind->name : ""),
			kind->enabled ? "" : " [off]");
	}
}
