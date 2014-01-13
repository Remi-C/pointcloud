/***********************************************************************
* pc_patch.c
*
*  Pointclound patch handling. Create, get and set values from the
*  basic PCPATCH structure.
*
*  PgSQL Pointcloud is free and open source software provided
*  by the Government of Canada
*  Copyright (c) 2013 Natural Resources Canada
*
***********************************************************************/

#include <math.h>
#include <assert.h>
#include "pc_api_internal.h"
#include "stringbuffer.h"

int
pc_patch_compute_extent(PCPATCH *pa)
{
	if ( ! pa ) return PC_FAILURE;
	switch ( pa->type )
	{
	case PC_NONE:
		return pc_patch_uncompressed_compute_extent((PCPATCH_UNCOMPRESSED*)pa);
	case PC_GHT:
		return pc_patch_ght_compute_extent((PCPATCH_GHT*)pa);
	case PC_DIMENSIONAL:
		return pc_patch_dimensional_compute_extent((PCPATCH_DIMENSIONAL*)pa);
	}
	return PC_FAILURE;
}

/**
* Calculate or re-calculate statistics for a patch.
*/
int
pc_patch_compute_stats(PCPATCH *pa)
{
	if ( ! pa ) return PC_FAILURE;

	switch ( pa->type )
	{
	case PC_NONE:
		return pc_patch_uncompressed_compute_stats((PCPATCH_UNCOMPRESSED*)pa);

	case PC_DIMENSIONAL:
	{
		PCPATCH_UNCOMPRESSED *pu = pc_patch_uncompressed_from_dimensional((PCPATCH_DIMENSIONAL*)pa);
		pc_patch_uncompressed_compute_stats(pu);
		pa->stats = pc_stats_clone(pu->stats);
		pc_patch_uncompressed_free(pu);
		return PC_SUCCESS;
	}
	case PC_GHT:
	{
		PCPATCH_UNCOMPRESSED *pu = pc_patch_uncompressed_from_ght((PCPATCH_GHT*)pa);
		pc_patch_uncompressed_compute_stats(pu);
		pa->stats = pc_stats_clone(pu->stats);
		pc_patch_uncompressed_free(pu);
		return PC_SUCCESS;
	}
	default:
	{
		pcerror("%s: unknown compression type", __func__, pa->type);
		return PC_FAILURE;
	}
	}

	return PC_FAILURE;
}

void
pc_patch_free(PCPATCH *patch)
{
	if ( patch->stats )
	{
		pc_stats_free( patch->stats );
		patch->stats = NULL;
	}

	switch( patch->type )
	{
	case PC_NONE:
	{
		pc_patch_uncompressed_free((PCPATCH_UNCOMPRESSED*)patch);
		break;
	}
	case PC_GHT:
	{
		pc_patch_ght_free((PCPATCH_GHT*)patch);
		break;
	}
	case PC_DIMENSIONAL:
	{
		pc_patch_dimensional_free((PCPATCH_DIMENSIONAL*)patch);
		break;
	}
	default:
	{
		pcerror("%s: unknown compression type %d", __func__, patch->type);
		break;
	}
	}
}


PCPATCH *
pc_patch_from_pointlist(const PCPOINTLIST *ptl)
{
	return (PCPATCH*)pc_patch_uncompressed_from_pointlist(ptl);
}


PCPATCH *
pc_patch_compress(const PCPATCH *patch, void *userdata)
{
	uint32_t schema_compression = patch->schema->compression;
	uint32_t patch_compression = patch->type;

	switch ( schema_compression )
	{
	case PC_DIMENSIONAL:
	{
		if ( patch_compression == PC_NONE )
		{
			/* Dimensionalize, dimensionally compress, return */
			PCPATCH_DIMENSIONAL *pcdu = pc_patch_dimensional_from_uncompressed((PCPATCH_UNCOMPRESSED*)patch);
			PCPATCH_DIMENSIONAL *pcdd = pc_patch_dimensional_compress(pcdu, (PCDIMSTATS*)userdata);
			pc_patch_dimensional_free(pcdu);
			return (PCPATCH*)pcdd;
		}
		else if ( patch_compression == PC_DIMENSIONAL )
		{
			/* Make sure it's compressed, return */
			return (PCPATCH*)pc_patch_dimensional_compress((PCPATCH_DIMENSIONAL*)patch, (PCDIMSTATS*)userdata);
		}
		else if ( patch_compression == PC_GHT )
		{
			/* Uncompress, dimensionalize, dimensionally compress, return */
			PCPATCH_UNCOMPRESSED *pcu = pc_patch_uncompressed_from_ght((PCPATCH_GHT*)patch);
			PCPATCH_DIMENSIONAL *pcdu  = pc_patch_dimensional_from_uncompressed(pcu);
			PCPATCH_DIMENSIONAL *pcdc  = pc_patch_dimensional_compress(pcdu, NULL);
			pc_patch_dimensional_free(pcdu);
			return (PCPATCH*)pcdc;
		}
		else
		{
			pcerror("%s: unknown patch compression type %d", __func__, patch_compression);
		}
	}
	case PC_NONE:
	{
		if ( patch_compression == PC_NONE )
		{
			return (PCPATCH*)patch;
		}
		else if ( patch_compression == PC_DIMENSIONAL )
		{
			PCPATCH_UNCOMPRESSED *pcu = pc_patch_uncompressed_from_dimensional((PCPATCH_DIMENSIONAL*)patch);
			return (PCPATCH*)pcu;

		}
		else if ( patch_compression == PC_GHT )
		{
			PCPATCH_UNCOMPRESSED *pcu = pc_patch_uncompressed_from_ght((PCPATCH_GHT*)patch);
			return (PCPATCH*)pcu;
		}
		else
		{
			pcerror("%s: unknown patch compression type %d", __func__, patch_compression);
		}
	}
	case PC_GHT:
	{
		if ( patch_compression == PC_NONE )
		{
			PCPATCH_GHT *pgc = pc_patch_ght_from_uncompressed((PCPATCH_UNCOMPRESSED*)patch);
			return (PCPATCH*)pgc;
		}
		else if ( patch_compression == PC_DIMENSIONAL )
		{
			PCPATCH_UNCOMPRESSED *pcu = pc_patch_uncompressed_from_dimensional((PCPATCH_DIMENSIONAL*)patch);
			PCPATCH_GHT *pgc = pc_patch_ght_from_uncompressed((PCPATCH_UNCOMPRESSED*)patch);
			pc_patch_uncompressed_free(pcu);
			return (PCPATCH*)pgc;
		}
		else if ( patch_compression == PC_GHT )
		{
			return (PCPATCH*)patch;
		}
		else
		{
			pcerror("%s: unknown patch compression type %d", __func__, patch_compression);
		}
	}
	default:
	{
		pcerror("%s: unknown schema compression type %d", __func__, schema_compression);
	}
	}

	pcerror("%s: fatal error", __func__);
	return NULL;
}


PCPATCH *
pc_patch_uncompress(const PCPATCH *patch)
{
	uint32_t patch_compression = patch->type;

	if ( patch_compression == PC_DIMENSIONAL )
	{
		PCPATCH_UNCOMPRESSED *pu = pc_patch_uncompressed_from_dimensional((PCPATCH_DIMENSIONAL*)patch);
		return (PCPATCH*)pu;
	}

	if ( patch_compression == PC_NONE )
	{
		return (PCPATCH*)patch;
	}

	if ( patch_compression == PC_GHT )
	{
		PCPATCH_UNCOMPRESSED *pu = pc_patch_uncompressed_from_ght((PCPATCH_GHT*)patch);
		return (PCPATCH*)pu;
	}

	return NULL;
}



PCPATCH *
pc_patch_from_wkb(const PCSCHEMA *s, uint8_t *wkb, size_t wkbsize)
{
	/*
	byte:     endianness (1 = NDR, 0 = XDR)
	uint32:   pcid (key to POINTCLOUD_SCHEMAS)
	uint32:   compression (0 = no compression, 1 = dimensional, 2 = GHT)
	uchar[]:  data (interpret relative to pcid and compression)
	*/
	uint32_t compression, pcid;
	PCPATCH *patch;

	if ( ! wkbsize )
	{
		pcerror("%s: zero length wkb", __func__);
	}

	/*
	* It is possible for the WKB compression to be different from the
	* schema compression at this point. The schema compression is only
	* forced at serialization time.
	*/
	pcid = wkb_get_pcid(wkb);
	compression = wkb_get_compression(wkb);

	if ( pcid != s->pcid )
	{
		pcerror("%s: wkb pcid (%d) not consistent with schema pcid (%d)", __func__, pcid, s->pcid);
	}

	switch ( compression )
	{
	case PC_NONE:
	{
		patch = pc_patch_uncompressed_from_wkb(s, wkb, wkbsize);
		break;
	}
	case PC_DIMENSIONAL:
	{
		patch = pc_patch_dimensional_from_wkb(s, wkb, wkbsize);
		break;
	}
	case PC_GHT:
	{
		patch = pc_patch_ght_from_wkb(s, wkb, wkbsize);
		break;
	}
	default:
	{
		/* Don't get here */
		pcerror("%s: unknown compression '%d' requested", __func__, compression);
		return NULL;
	}
	}

	if ( PC_FAILURE == pc_patch_compute_extent(patch) )
		pcerror("%s: pc_patch_compute_extent failed", __func__);

	if ( PC_FAILURE == pc_patch_compute_stats(patch) )
		pcerror("%s: pc_patch_compute_stats failed", __func__);

	return patch;

}



uint8_t *
pc_patch_to_wkb(const PCPATCH *patch, size_t *wkbsize)
{
	/*
	byte:     endianness (1 = NDR, 0 = XDR)
	uint32:   pcid (key to POINTCLOUD_SCHEMAS)
	uint32:   compression (0 = no compression, 1 = dimensional, 2 = GHT)
	uchar[]:  data (interpret relative to pcid and compression)
	*/
	switch ( patch->type )
	{
	case PC_NONE:
	{
		return pc_patch_uncompressed_to_wkb((PCPATCH_UNCOMPRESSED*)patch, wkbsize);
	}
	case PC_DIMENSIONAL:
	{
		return pc_patch_dimensional_to_wkb((PCPATCH_DIMENSIONAL*)patch, wkbsize);
	}
	case PC_GHT:
	{
		return pc_patch_ght_to_wkb((PCPATCH_GHT*)patch, wkbsize);
	}
	}
	pcerror("%s: unknown compression requested '%d'", __func__, patch->schema->compression);
	return NULL;
}

char *
pc_patch_to_string(const PCPATCH *patch)
{
	switch( patch->type )
	{
	case PC_NONE:
		return pc_patch_uncompressed_to_string((PCPATCH_UNCOMPRESSED*)patch);
	case PC_DIMENSIONAL:
		return pc_patch_dimensional_to_string((PCPATCH_DIMENSIONAL*)patch);
	case PC_GHT:
		return pc_patch_ght_to_string((PCPATCH_GHT*)patch);
	}
	pcerror("%s: unsupported compression %d requested", __func__, patch->type);
	return NULL;
}





PCPATCH *
pc_patch_from_patchlist(PCPATCH **palist, int numpatches)
{
	int i;
	uint32_t totalpoints = 0;
	PCPATCH_UNCOMPRESSED *paout;
	const PCSCHEMA *schema = NULL;
	uint8_t *buf;

	assert(palist);
	assert(numpatches);

	/* All schemas better be the same... */
	schema = palist[0]->schema;

	/* How many points will this output have? */
	for ( i = 0; i < numpatches; i++ )
	{
		if ( schema->pcid != palist[i]->schema->pcid )
		{
			pcerror("%s: inconsistent schemas in input", __func__);
			return NULL;
		}
		totalpoints += palist[i]->npoints;
	}

	/* Blank output */
	paout = pc_patch_uncompressed_make(schema, totalpoints);
	buf = paout->data;

	/* Uncompress dimensionals, copy uncompressed */
	for ( i = 0; i < numpatches; i++ )
	{
		const PCPATCH *pa = palist[i];

		/* Update bounds */
		pc_bounds_merge(&(paout->bounds), &(pa->bounds));

		switch ( pa->type )
		{
		case PC_DIMENSIONAL:
		{
			PCPATCH_UNCOMPRESSED *pu = pc_patch_uncompressed_from_dimensional((const PCPATCH_DIMENSIONAL*)pa);
			size_t sz = pu->schema->size * pu->npoints;
			memcpy(buf, pu->data, sz);
			buf += sz;
			pc_patch_uncompressed_free(pu);
			break;
		}
		case PC_GHT:
		{
			PCPATCH_UNCOMPRESSED *pu = pc_patch_uncompressed_from_ght((const PCPATCH_GHT*)pa);
			size_t sz = pu->schema->size * pu->npoints;
			memcpy(buf, pu->data, sz);
			buf += sz;
			pc_patch_uncompressed_free(pu);
			break;
		}
		case PC_NONE:
		{
			PCPATCH_UNCOMPRESSED *pu = (PCPATCH_UNCOMPRESSED*)pa;
			size_t sz = pu->schema->size * pu->npoints;
			memcpy(buf, pu->data, sz);
			buf += sz;
			break;
		}
		default:
		{
			pcerror("%s: unknown compression type (%d)", __func__, pa->type);
			break;
		}
		}
	}

	paout->npoints = totalpoints;

	if ( PC_FAILURE == pc_patch_uncompressed_compute_stats(paout) )
	{
		pcerror("%s: stats computation failed", __func__);
		return NULL;
	}

	return (PCPATCH*)paout;
}

/**
 * @param
 * @param
 * @param
 * @return the input patch with less dimensions, only dimension keept are these in provided array 
 */
PCPATCH * 
pc_patch_reduce_dimension(PCPATCH *patch, char ** dim_to_keep, uint32_t dimensions_number)
{
	//switch on patch compression type
	//@TODO : only dimensionnal supported
	
	/** pseudo code of the function
	 * test code : will be removed 
	 * switch to adapt strategy based on compression type
		* 	case dimensionnal compression:
		* 		
	*/
	PCPATCH *paout;
	
	
	//get dimension number from name for "X", "Y", "Z" into an array of int 
		//create array of int of size 3 
		// get dimension position from dimension name into int array
		uint32_t new_dim_number = 2; 
		//char *dim_to_keep[] = { "x", "Z"};
		uint32_t dim_position[3] = { -1,-1 };
		
		/*
		int i2 ;
		for(i2=0;i2<new_dim_number;i2++)
		{
			dim_position[i2] = pc_schema_get_dimension_position_by_name(patch->schema, dim_to_keep[i2]);
			printf("\n dimension %s has position %d",dim_to_keep[i2],dim_position[i2] );
		}
		printf("\n");
		*/
		int i2 ;
		for(i2=0;i2<dimensions_number;i2++)
		{
			dim_position[i2] = pc_schema_get_dimension_position_by_name(patch->schema, dim_to_keep[i2]);
			printf("\n dimension %s has position %d",dim_to_keep[i2],dim_position[i2] );
		}
		printf("\n");
	
	switch ( patch->type )
	{
	case PC_NONE:
	{
		pcerror("%s: error : trying to reduce dimension of a non dimensionnal-compressed patch, not yet supported", __func__);
	 
		break;
	}
	case PC_GHT:
	{
			pcerror("%s: error : trying to reduce dimension of a non dimensionnal-compressed patch, not yet supported", __func__);
		 
		break;
	}
	case PC_DIMENSIONAL:
	{
		
			PCSCHEMA* temp_schema;
			
			//we create a new version of the patch with less dimension
			PCPATCH_DIMENSIONAL *o_patch = pcalloc(sizeof(PCPATCH_DIMENSIONAL));
					//cloning the patch structure
					memcpy(o_patch, patch, sizeof(PCPATCH_DIMENSIONAL));
					//cloning the schema with a subset of dimension
						o_patch->schema =  pc_schema_clone_subset(patch->schema, dim_position, new_dim_number);
					
					printf("\n schema to json : %s \n",pc_schema_to_json(o_patch->schema), new_dim_number);//test  :
					
					//cloning the dimstats with a subset of dimension
					
					//cloning the bytes with a subset of dimension
				
				//checking the validity of patch

			//clone the patch into a new patch, without cloning data
				
				
				
				//initalyze to 0 the bytes structure 
				o_patch->bytes = pcalloc( new_dim_number  * sizeof(PCBYTES));
				pc_bytes_empty(o_patch->bytes);
				o_patch->npoints =  ((PCPATCH_DIMENSIONAL*)patch)->npoints;
		
		/*
				o_patch->stats = ((PCPATCH_DIMENSIONAL*)patch)->stats;
			
			
				//modifying the schema to keep only necessary dimensions
					//modifying the schema
					//free schema
					pc_schema_free(o_patch);
					//create new empty schema
					
					o_patch->schema->ndims = new_dim_number
					
					//copying only necessary dimension
				//slight change of schema_clone, 	
					int j;
					PCDIMENSION *temp_pcdimension = pcalloc(sizeof(PCDIMENSION));
					
					o_patch->schema = pc_schema_new(new_dim_number);
					o_patch->schema->pcid = patch->schema->pcid;
					o_patch->schema->srid = patch->schema->srid;
					o_patch->schema->x_position = patch->schema->x_position;
					o_patch->schema->y_position = patch->schema->y_position;
					o_patch->schema->compression = patch->schema->compression;
					
					
					for ( j = 0; j < new_dim_number; i++ )//loop on all the dimension we want to keep
					{//adding the dimension if it is one we have to keep
						
						//cloning old dimension into a  new one
						temp_pcdimension = pc_dimension_clone(pc_schema_get_dimension_by_name(dim_to_keep[j]));
						//change the position to "j"
						temp_pcdimension->position=j;
						
						//adding this new dimension to new patch
							pc_schema_set_dimension(o_patch->schema, temp_pcdimension);
						}
					}
					
					
					pc_schema_calculate_byteoffsets(pcs);
					* 
					* */
					
			/*		
			TO CHANGE : we must fil the bit in the right order because dimension order have been changed.
			//file the bytes , allocating when necessary :
			int i = 0;
			for ( i = 0; i < new_dim_number; i++ )
				{
					o_patch->bytes[i] = ((PCPATCH_DIMENSIONAL*)patch)->bytes[i];
				}
			*/ 
			
			paout =  (PCPATCH *)o_patch ;
			break;
			//pcerror("%s: error : function not yet completed !", __func__);
			
	}
	default:
		pcerror("%s: failure", __func__);
	}
		
	//returning the result
	return paout;
}












