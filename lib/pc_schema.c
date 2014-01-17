/***********************************************************************
* pc_schema.c
*
*  Pointclound schema handling. Parse and emit the XML format for
*  representing packed multidimensional point data.
*
*  PgSQL Pointcloud is free and open source software provided
*  by the Government of Canada
*  Copyright (c) 2013 Natural Resources Canada
*
***********************************************************************/

#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "pc_api_internal.h"
#include "stringbuffer.h"


/** Convert XML string token to type interpretation number */
const char *
pc_interpretation_string(uint32_t interp)
{
	if ( interp < NUM_INTERPRETATIONS )
		return INTERPRETATION_STRINGS[interp];
	else
		return "unknown";
}


/** Convert XML string token to type interpretation number */
static int
pc_interpretation_number(const char *str)
{
	if ( str[0] == 'i' || str[0] == 'I' )
	{
		if ( str[3] == '8' )
			return PC_INT8;
		if ( str[3] == '1' )
			return PC_INT16;
		if ( str[3] == '3' )
			return PC_INT32;
		if ( str[3] == '6' )
			return PC_INT64;
	}
	else if ( str[0] == 'u' || str[0] == 'U' )
	{
		if ( str[4] == '8' )
			return PC_UINT8;
		if ( str[4] == '1' )
			return PC_UINT16;
		if ( str[4] == '3' )
			return PC_UINT32;
		if ( str[4] == '6' )
			return PC_UINT64;
	}
	else if ( str[0] == 'd' || str[0] == 'D' )
	{
		return PC_DOUBLE;
	}
	else if ( str[0] == 'f' || str[0] == 'F' )
	{
		return PC_FLOAT;
	}
	else
		return PC_UNKNOWN;
	
	return PC_UNKNOWN;
}

static int
pc_compression_number(const char *str)
{
	if ( ! str )
		return PC_NONE;

	if ( (str[0] == 'd' || str[0] == 'D') &&
	        (strcasecmp(str, "dimensional") == 0) )
	{
		return PC_DIMENSIONAL;
	}

	if ( (str[0] == 'g' || str[0] == 'G') &&
	        (strcasecmp(str, "ght") == 0) )
	{
		return PC_GHT;
	}

	if ( (str[0] == 'n' || str[0] == 'N') &&
	        (strcasecmp(str, "none") == 0) )
	{
		return PC_NONE;
	}

	return PC_NONE;
}

/** Convert type interpretation number size in bytes */
size_t
pc_interpretation_size(uint32_t interp)
{
	if ( interp < NUM_INTERPRETATIONS )
	{
		return INTERPRETATION_SIZES[interp];
	}
	else
	{
		pcerror("pc_interpretation_size: invalid interpretation");
		return 0;
	}
}

/** Allocate clean memory for a PCDIMENSION struct */
static PCDIMENSION*
pc_dimension_new()
{
	PCDIMENSION *pcd = pcalloc(sizeof(PCDIMENSION));
	/* Default scaling value is 1! */
	pcd->scale = 1.0;
	return pcd;
}

static PCDIMENSION*
pc_dimension_clone(const PCDIMENSION *dim)
{
	PCDIMENSION *pcd = pc_dimension_new();
	/* Copy all the inline data */
	memcpy(pcd, dim, sizeof(PCDIMENSION));
	/* Copy the referenced data */
	pcd->name = pcstrdup(dim->name);
	pcd->description = pcstrdup(dim->description);
	return pcd;
}

/** Release the memory behind the PCDIMENSION struct */
static void
pc_dimension_free(PCDIMENSION *pcd)
{
	/* Assumption: No memory in the dimension is owned somewhere else */
	if ( pcd->description )
		pcfree(pcd->description);
	if ( pcd->name )
		pcfree(pcd->name);
	pcfree(pcd);
}

static PCSCHEMA*
pc_schema_new(uint32_t ndims)
{
	PCSCHEMA *pcs = pcalloc(sizeof(PCSCHEMA));
	pcs->dims = pcalloc(sizeof(PCDIMENSION*) * ndims);
	pcs->namehash = create_string_hashtable();
	pcs->ndims = ndims;
	pcs->x_position = -1;
	pcs->y_position = -1;
	return pcs;
}

/** Complete the byte offsets of dimensions from the ordered sizes */
static void
pc_schema_calculate_byteoffsets(PCSCHEMA *pcs)
{
	int i;
	size_t byteoffset = 0;
	for ( i = 0; i < pcs->ndims; i++ )
	{
		if ( pcs->dims[i] )
		{
			pcs->dims[i]->byteoffset = byteoffset;
			pcs->dims[i]->size = pc_interpretation_size(pcs->dims[i]->interpretation);
			byteoffset += pcs->dims[i]->size;
		}
	}
	pcs->size = byteoffset;
}

void
pc_schema_set_dimension(PCSCHEMA *s, PCDIMENSION *d)
{
	s->dims[d->position] = d;
	hashtable_insert(s->namehash, d->name, d);
	pc_schema_calculate_byteoffsets(s);
}


PCSCHEMA*
pc_schema_clone(const PCSCHEMA *s)
{
	int i;
	PCSCHEMA *pcs = pc_schema_new(s->ndims);
	pcs->pcid = s->pcid;
	pcs->srid = s->srid;
	pcs->x_position = s->x_position;
	pcs->y_position = s->y_position;
	pcs->compression = s->compression;
	for ( i = 0; i < pcs->ndims; i++ )
	{
		if ( s->dims[i] )
		{
			pc_schema_set_dimension(pcs, pc_dimension_clone(s->dims[i]));
		}
	}
	pc_schema_calculate_byteoffsets(pcs);
	return pcs;
}
/**
 * @param schema we want to partially clone
 * @param an array with the position of the dimension we want to *keep*
 * @param the total number of dimension we want to keep
 * @return a cloned schema with only the dimension whose position are provided, the new position of dimension is there index+1 in provided array, NULL if something wen wrong
 * */
PCSCHEMA * 
pc_schema_clone_subset(  PCSCHEMA *s, uint32_t * dimensions_position_array, uint32_t dimensions_number)
{
	
	int i;
	PCDIMENSION* temp_dim;
	PCSCHEMA *pcs = pc_schema_new(dimensions_number);
	pcs->pcid = s->pcid;
	pcs->srid = s->srid;
	
	pcs->compression = s->compression;
	
	//printf("\n for loop \n");
	for ( i = 0; i < dimensions_number; i++ )
	{
		if ( s->dims[dimensions_position_array[i]] )
		{
				temp_dim = pc_dimension_clone(s->dims[dimensions_position_array[i]]);//cloning the dimension
				temp_dim->position=i ; //changing the position so to have continuous dimension position
				
			pc_schema_set_dimension(pcs,temp_dim);
			//printf("cloning the dimension %s in new position %d",temp_dim->name,i);
		}
	}
	
	//the new x_position and y_position should be found according to where X and Y dimension are
		//looking for X and Y in dimension, to get the dimension position
	pcs->x_position=pc_schema_get_dimension_position_by_name(pcs,"x");
	pcs->y_position=pc_schema_get_dimension_position_by_name(pcs,"y");

	//pcinfo("updating schema : x_position : %d, y_position : %d",pcs->x_position,pcs->y_position);
	pc_schema_calculate_byteoffsets(pcs);

	return pcs;
}


/** Release the memory behind the PCSCHEMA struct */
void
pc_schema_free(PCSCHEMA *pcs)
{
	int i;

	for ( i = 0; i < pcs->ndims; i++ )
	{
		if ( pcs->dims[i] )
		{
			pc_dimension_free(pcs->dims[i]);
			pcs->dims[i] = 0;
		}
	}
	pcfree(pcs->dims);

	if ( pcs->namehash )
		hashtable_destroy(pcs->namehash, 0);

	pcfree(pcs);
}

/** Convert a PCSCHEMA to a human-readable JSON string */
char *
pc_schema_to_json(const PCSCHEMA *pcs)
{
	int i;
	char *str;
	stringbuffer_t *sb = stringbuffer_create();
	stringbuffer_append(sb, "{");

	if ( pcs->pcid )
		stringbuffer_aprintf(sb, "\"pcid\" : %d,\n", pcs->pcid);
	if ( pcs->ndims )
		stringbuffer_aprintf(sb, "\"ndims\" : %d,\n", pcs->ndims);
	if ( pcs->srid )
		stringbuffer_aprintf(sb, "\"srid\" : %d,\n", pcs->srid);
	if ( pcs->compression )
		stringbuffer_aprintf(sb, "\"compression\" : %d,\n", pcs->compression);
	if ( pcs->size )
		stringbuffer_aprintf(sb, "\"size\" : %zu,\n", pcs->size);
	if ( pcs->x_position>=0 )
		stringbuffer_aprintf(sb, "\"x_position\" : %d,\n", pcs->x_position);
	if ( pcs->y_position >=0)
		stringbuffer_aprintf(sb, "\"y_position\" : %d,\n", pcs->y_position);
	if ( pcs->namehash->entrycount )
		stringbuffer_aprintf(sb, "\"namehash->entrycount\" : %d,\n", pcs->namehash->entrycount);
	if ( pcs->ndims )
	{

		stringbuffer_append(sb, "\"dims\" : [\n");

		for ( i = 0; i < pcs->ndims; i++ )
		{
		 
		  stringbuffer_append(sb,pc_dimension_to_json(pcs->dims[i] ) );  
			 
		}
		stringbuffer_append(sb, "\n]\n");
	}
	stringbuffer_append(sb, "}\n");
	str = stringbuffer_getstringcopy(sb);
	stringbuffer_destroy(sb);
	return str;
}

void pc_schema_check_xy(PCSCHEMA *s)
{
	int i;
	for ( i = 0; i < s->ndims; i++ )
	{
		char *dimname = s->dims[i]->name;
		if ( strcasecmp(dimname, "X") == 0 ||
		        strcasecmp(dimname, "Longitude") == 0 ||
		        strcasecmp(dimname, "Lon") == 0 )
		{
			s->x_position = i;
			continue;
		}
		if ( strcasecmp(dimname, "Y") == 0 ||
		        strcasecmp(dimname, "Latitude") == 0 ||
		        strcasecmp(dimname, "Lat") == 0 )
		{
			s->y_position = i;
			continue;
		}
	}

	if ( s->x_position < 0 )
		pcerror("pc_schema_check_xy: invalid x_position '%d'", s->x_position);

	if ( s->y_position < 0 )
		pcerror("pc_schema_check_xy: invalid y_position '%d'", s->y_position);
}


/**@brief * Convert a PCSDIMENSION to a human-readable JSON string 
 * @param the PCDIMENSION we want to print
 * @return q pointer to the string buff describing the object
 * */
char *
pc_dimension_to_json(const PCDIMENSION *d)
{
	int i;
	char *str;
	stringbuffer_t *sb = stringbuffer_create();
	stringbuffer_append(sb, "{");

	 
			if ( d )
			{
				 

				if ( i ) stringbuffer_append(sb, ",");
				stringbuffer_append(sb, "\n { \n");

				if ( d->name )
					stringbuffer_aprintf(sb, "  \"name\" : \"%s\",\n", d->name);
				if ( d->description )
					stringbuffer_aprintf(sb, "  \"description\" : \"%s\",\n", d->description);

				stringbuffer_aprintf(sb, "  \"size\" : %d,\n", d->size);
				stringbuffer_aprintf(sb, "  \"byteoffset\" : %d,\n", d->byteoffset);
				stringbuffer_aprintf(sb, "  \"scale\" : %g,\n", d->scale);
				stringbuffer_aprintf(sb, "  \"interpretation\" : \"%s\",\n", pc_interpretation_string(d->interpretation));
				stringbuffer_aprintf(sb, "  \"offset\" : %g,\n", d->offset);

				stringbuffer_aprintf(sb, "  \"active\" : %d\n", d->active);
				stringbuffer_append(sb, " }");
			}
		 
	stringbuffer_append(sb, "}\n");
	str = stringbuffer_getstringcopy(sb);
	stringbuffer_destroy(sb);
	return str;
}



static char *
xml_node_get_content(xmlNodePtr node)
{
	int i;
	xmlNodePtr cur = node->children;
	if ( cur )
	{
		do
		{
			if ( cur->type == XML_TEXT_NODE )
			{
				return (char*)(cur->content);
			}
		}
		while ( (cur = cur->next) );
	}
	return "";
}

/** Population a PCSCHEMA struct from the XML representation */
int
pc_schema_from_xml(const char *xml_str, PCSCHEMA **schema)
{
	xmlDocPtr xml_doc = NULL;
	xmlNodePtr xml_root = NULL;
	xmlNsPtr xml_ns = NULL;
	xmlXPathContextPtr xpath_ctx;
	xmlXPathObjectPtr xpath_obj;
	xmlNodeSetPtr nodes;
	PCSCHEMA *s;
	const char *xml_ptr = xml_str;

	/* Roll forward to start of XML string */
	while( (*xml_ptr != '\0') && (*xml_ptr != '<') )
	{
		xml_ptr++;
	}

	size_t xml_size = strlen(xml_ptr);
	static xmlChar *xpath_str = (xmlChar*)("/pc:PointCloudSchema/pc:dimension");
	static xmlChar *xpath_metadata_str = (xmlChar*)("/pc:PointCloudSchema/pc:metadata/Metadata");


	/* Parse XML doc */
	*schema = NULL;
	xmlInitParser();
	xml_doc = xmlReadMemory(xml_ptr, xml_size, NULL, NULL, 0);
	if ( ! xml_doc )
	{
		xmlCleanupParser();
		pcwarn("unable to parse schema XML");
		return PC_FAILURE;
	}

	/* Capture the namespace */
	xml_root = xmlDocGetRootElement(xml_doc);
	if ( xml_root->ns )
		xml_ns = xml_root->ns;

	/* Create xpath evaluation context */
	xpath_ctx = xmlXPathNewContext(xml_doc);
	if( ! xpath_ctx )
	{
		xmlFreeDoc(xml_doc);
		xmlCleanupParser();
		pcwarn("unable to create new XPath context to read schema XML");
		return PC_FAILURE;
	}

	/* Register the root namespace if there is one */
	if ( xml_ns )
		xmlXPathRegisterNs(xpath_ctx, (xmlChar*)"pc", xml_ns->href);

	/* Evaluate xpath expression */
	xpath_obj = xmlXPathEvalExpression(xpath_str, xpath_ctx);
	if( ! xpath_obj )
	{
		xmlXPathFreeContext(xpath_ctx);
		xmlFreeDoc(xml_doc);
		xmlCleanupParser();
		pcwarn("unable to evaluate xpath expression \"%s\" against schema XML", xpath_str);
		return PC_FAILURE;
	}

	/* Iterate on the dimensions we found */
	if ( (nodes = xpath_obj->nodesetval) )
	{
		int ndims = nodes->nodeNr;
		int i;
		s = pc_schema_new(ndims);
		*schema = s;

		for ( i = 0; i < ndims; i++ )
		{
			/* This is a "dimension" */
			if( nodes->nodeTab[i]->type == XML_ELEMENT_NODE )
			{
				xmlNodePtr cur = nodes->nodeTab[i];
				xmlNodePtr child;
				PCDIMENSION *d = pc_dimension_new();
				char xydim = 0;

				/* These are the values of the dimension */
				for ( child = cur->children; child; child = child->next )
				{
					if( child->type == XML_ELEMENT_NODE )
					{
						char *content = (char*)(child->children->content);
						char *name = (char*)(child->name);
						if ( strcmp(name, "name") == 0 )
						{
							if ( strcasecmp(content, "X") == 0 ||
							        strcasecmp(content, "Longitude") == 0 ||
							        strcasecmp(content, "Lon") == 0 )
							{
								xydim = 'x';
							}
							if ( strcasecmp(content, "Y") == 0 ||
							        strcasecmp(content, "Latitude") == 0 ||
							        strcasecmp(content, "Lat") == 0 )
							{
								xydim = 'y';
							}
							d->name = pcstrdup(content);
						}
						else if ( strcmp(name, "description") == 0 )
							d->description = pcstrdup(content);
						else if ( strcmp(name, "size") == 0 )
							d->size = atoi(content);
						else if ( strcmp(name, "active") == 0 )
							d->active = atoi(content);
						else if ( strcmp(name, "position") == 0 )
							d->position = atoi(content) - 1;
						else if ( strcmp(name, "interpretation") == 0 )
							d->interpretation = pc_interpretation_number(content);
						else if ( strcmp(name, "scale") == 0 )
							d->scale = atof(content);
						else if ( strcmp(name, "offset") == 0 )
							d->offset = atof(content);
						else if ( strcmp(name, "uuid") == 0 )
							/* Ignore this tag for now */ {}
						else if ( strcmp(name, "parent_uuid") == 0 )
							/* Ignore this tag for now */ {}
						else
							pcinfo("unhandled schema type element \"%s\" encountered", name);
					}
				}

				/* Convert interprestation to size */
				d->size = pc_interpretation_size(d->interpretation);

				/* Store the dimension in the schema */
				if ( d->position < ndims )
				{
					if ( s->dims[d->position] )
					{
						xmlXPathFreeObject(xpath_obj);
						xmlXPathFreeContext(xpath_ctx);
						xmlFreeDoc(xml_doc);
						xmlCleanupParser();
						pc_schema_free(s);
						pcwarn("schema dimension at position \"%d\" is declared twice", d->position + 1, ndims);
						return PC_FAILURE;
					}
					if ( xydim == 'x' )
					{
						s->x_position = d->position;
					}
					if ( xydim == 'y' )
					{
						s->y_position = d->position;
					}
					pc_schema_set_dimension(s, d);
				}
				else
				{
					xmlXPathFreeObject(xpath_obj);
					xmlXPathFreeContext(xpath_ctx);
					xmlFreeDoc(xml_doc);
					xmlCleanupParser();
					pc_schema_free(s);
					pcwarn("schema dimension states position \"%d\", but number of XML dimensions is \"%d\"", d->position + 1, ndims);
					return PC_FAILURE;
				}
			}
		}

		/* Complete the byte offsets of dimensions from the ordered sizes */
		pc_schema_calculate_byteoffsets(s);
		/* Check X/Y positions */
		pc_schema_check_xy(s);
	}

	xmlXPathFreeObject(xpath_obj);

	/* SEARCH FOR METADATA ENTRIES */
	xpath_obj = xmlXPathEvalExpression(xpath_metadata_str, xpath_ctx);
	if( ! xpath_obj )
	{
		xmlXPathFreeContext(xpath_ctx);
		xmlFreeDoc(xml_doc);
		xmlCleanupParser();
		pcwarn("unable to evaluate xpath expression \"%s\" against schema XML", xpath_metadata_str);
		return PC_FAILURE;
	}

	/* Iterate on the <Metadata> we find */
	if ( (nodes = xpath_obj->nodesetval) )
	{
		int i;

		for ( i = 0; i < nodes->nodeNr; i++ )
		{
			char *metadata_name = "";
			char *metadata_value = "";
			/* Read the metadata name and value from the node */
			/* <Metadata name="somename">somevalue</Metadata> */
			xmlNodePtr cur = nodes->nodeTab[i];
			if( cur->type == XML_ELEMENT_NODE && strcmp((char*)(cur->name), "Metadata") == 0 )
			{
				metadata_name = (char*)xmlGetProp(cur, (xmlChar*)"name");
				metadata_value = xml_node_get_content(cur);
			}

			/* Store the compression type on the schema */
			if ( strcmp(metadata_name, "compression") == 0 )
			{
				int compression = pc_compression_number(metadata_value);
				if ( compression >= 0 )
				{
					s->compression = compression;
				}
			}
			xmlFree(metadata_name);
		}
	}

	xmlXPathFreeObject(xpath_obj);

	xmlXPathFreeContext(xpath_ctx);
	xmlFreeDoc(xml_doc);
	xmlCleanupParser();

	return PC_SUCCESS;
}

uint32_t
pc_schema_is_valid(const PCSCHEMA *s)
{
	int i;

	if ( s->x_position < 0 )
	{
		pcwarn("schema does not include an X coordinate");
		return PC_FALSE;
	}

	if ( s->y_position < 0 )
	{
		pcwarn("schema does not include a Y coordinate");
		return PC_FALSE;
	}

	if ( ! s->ndims )
	{
		pcwarn("schema has no dimensions");
		return PC_FALSE;
	}

	for ( i = 0; i < s->ndims; i++ )
	{
		if ( ! s->dims[i] )
		{
			pcwarn("schema is missing a dimension at position %d", i);
			return PC_FALSE;
		}
	}

	return PC_TRUE;
}

PCDIMENSION *
pc_schema_get_dimension(const PCSCHEMA *s, uint32_t dim)
{
	if ( s && s->ndims > dim )
	{
		return s->dims[dim];
	}
	return NULL;
}

PCDIMENSION *
pc_schema_get_dimension_by_name(const PCSCHEMA *s, const char *name)
{
	if ( ! ( s && s->namehash ) )
		return NULL;

	return hashtable_search(s->namehash, name);
}

/**
 * This function returns the position of a dimension of the given name, or -1 if there is no dimension of the given name
 * 
 * */
uint32_t 
pc_schema_get_dimension_position_by_name(const PCSCHEMA *s, const char *name)
{
		PCDIMENSION * temp ; 
		temp = pc_schema_get_dimension_by_name(s, name);
		if(temp == NULL) {
			return -1;
			pcerror("error, the dimension %s doesn't exit in the schema %s \n",name,pc_schema_to_json(s));
		}
		else {
			return temp->position;
		}
		
}

size_t
pc_schema_get_size(const PCSCHEMA *s)
{
	return s->size;
}

