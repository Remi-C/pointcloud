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
		//pcerror("input patch : %s",pc_patch_to_string(patch) );
	
	//switch on patch compression type
	//@TODO : only dimensionnal supported
	
	
	PCPATCH *paout;
	uint32_t    *dim_position;
	dim_position = (uint32_t *) pcalloc(sizeof(uint32_t)*dimensions_number);
	
	
	//get dimension postion from name "X", "Y", "Z" etc. into an array of int NOTE : we know that this dimensions exists
		int i2 ;
		for(i2=0;i2<dimensions_number;i2++)
		{
			dim_position[i2] = pc_schema_get_dimension_position_by_name(patch->schema, dim_to_keep[i2]);
			//printf("\n dimension %s has position %d",dim_to_keep[i2],dim_position[i2] );
			//pcinfo("\n dimension %s has position %d",dim_to_keep[i2],dim_position[i2] );
		}
	
	
	
	
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
		//pcinfo("patch type dimensionnal compression  : no problem\n");
			
			//we create a new version of the patch with less dimension
			PCPATCH_DIMENSIONAL *o_patch = pcalloc(sizeof(PCPATCH_DIMENSIONAL));
			
					//cloning the patch structure
					memcpy(o_patch, patch, sizeof(PCPATCH_DIMENSIONAL));								 
						//pcinfo("successfull memory copy");
					//cloning the schema with a subset of dimension
						o_patch->schema =  pc_schema_clone_subset(patch->schema, dim_position, dimensions_number);
					//cloning the dimstats with a subset of dimension :
						//What is the use of dimdstats? only if dimstats is an entry, else we need to compute it anyway, and regular function do it.
						//anyway the function exists and is // pc_dimstats_clone_subset(const PCDIMSTATS * d, uint32_t * dimensions_position_array, uint32_t dimensions_number)
					//cloning simple stats then updating schema to the reduced version 
						o_patch->stats = pc_stats_update_schema(pc_stats_clone(patch->stats), o_patch->schema);
					//cloning the bytes with a subset of dimension 
						o_patch->bytes = pc_patch_dimensional_clone_subset_of_bytes( ((PCPATCH_DIMENSIONAL *)patch)->bytes,  dim_position, dimensions_number);
				
				//pcinfo("all substructures have been cloned, and the content too\n");
				
			//checking the validity of created patch
			assert(o_patch);
				
				//pcinfo("\n the schema to json : %s \n",pc_schema_to_json(o_patch->schema));
				//pcinfo("\n the stats to json : %s \n",pc_stats_to_json(o_patch->stats));
				//pcinfo("\n \n \n the patch to json : %s \n" , pc_patch_to_string((PCPATCH * ) o_patch));
				
			paout =  (PCPATCH *)o_patch ;
			break;	
	}
	default:
		pcerror("%s: failure", __func__);
	}
	
	//cleaning 
	pcfree(dim_position);
	//returning the result
	return paout;
}












