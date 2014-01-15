/***********************************************************************
* pc_dimstats.c
*
*  Support for "dimensional compression", which is a catch-all
*  term for applying compression separately on each dimension
*  of a PCPATCH collection of PCPOINTS.
*
*  Depending on the character of the data, one of these schemes
*  will be used:
*
*  - run-length encoding
*  - significant-bit removal
*  - deflate
*
*  PgSQL Pointcloud is free and open source software provided
*  by the Government of Canada
*  Copyright (c) 2013 Natural Resources Canada
*
***********************************************************************/

#include <stdarg.h>
#include <assert.h>
#include "pc_api_internal.h"
#include "stringbuffer.h"


PCDIMSTATS *
pc_dimstats_make(const PCSCHEMA *schema)
{
	PCDIMSTATS *pds;
	pds = pcalloc(sizeof(PCDIMSTATS));
	pds->ndims = schema->ndims;
	pds->stats = pcalloc(pds->ndims * sizeof(PCDIMSTAT));
	return pds;
}

void
pc_dimstats_free(PCDIMSTATS *pds)
{
	if ( pds->stats )
		pcfree(pds->stats);
	pcfree(pds);
}
/*
typedef struct
{
    uint32_t total_runs;
    uint32_t total_commonbits;
    uint32_t recommended_compression;
} PCDIMSTAT;

typedef struct
{
    int32_t ndims;
    uint32_t total_points;
    uint32_t total_patches;
    PCDIMSTAT *stats;
} PCDIMSTATS;
*/

char *
pc_dimstats_to_string(const PCDIMSTATS *pds)
{
	int i;
	stringbuffer_t *sb = stringbuffer_create();
	char *str;

	stringbuffer_aprintf(sb,"{\"ndims\":%d,\"total_points\":%d,\"total_patches\":%d,\"dims\":[",
	                     pds->ndims,
	                     pds->total_points,
	                     pds->total_patches
	                    );

	for ( i = 0; i < pds->ndims; i++ )
	{
		if ( i ) stringbuffer_append(sb, ",");
		stringbuffer_aprintf(sb, "{\"total_runs\":%d,\"total_commonbits\":%d,\"recommended_compression\":%d}",
		                     pds->stats[i].total_runs,
		                     pds->stats[i].total_commonbits,
		                     pds->stats[i].recommended_compression
		                    );
	}
	stringbuffer_append(sb, "]}");

	str = stringbuffer_getstringcopy(sb);
	stringbuffer_destroy(sb);
	return str;
}

int
pc_dimstats_update(PCDIMSTATS *pds, const PCPATCH_DIMENSIONAL *pdl)
{
	int i, j;
	uint32_t nelems = pdl->npoints;
	const PCSCHEMA *schema = pdl->schema;

	/* Update global stats */
	pds->total_points += pdl->npoints;
	pds->total_patches += 1;

	/* Update dimensional stats */
	for ( i = 0; i < pds->ndims; i++ )
	{
		PCBYTES pcb = pdl->bytes[i];
		pds->stats[i].total_runs += pc_bytes_run_count(&pcb);
		pds->stats[i].total_commonbits += pc_bytes_sigbits_count(&pcb);
	}

	/* Update recommended compression schema */
	for ( i = 0; i < pds->ndims; i++ )
	{
		PCDIMENSION *dim = pc_schema_get_dimension(schema, i);
		/* Uncompressed size, foreach point, one value entry */
		double raw_size = pds->total_points * dim->size;
		/* RLE size, for each run, one count byte and one value entry */
		double rle_size = pds->stats[i].total_runs * (dim->size + 1);
		/* Sigbits size, for each patch, one header and n bits for each entry */
		double avg_commonbits_per_patch = pds->stats[i].total_commonbits / pds->total_patches;
		double avg_uniquebits_per_patch = 8*dim->size - avg_commonbits_per_patch;
		double sigbits_size = pds->total_patches * 2 * dim->size + pds->total_points * avg_uniquebits_per_patch / 8;
		/* Default to ZLib */
		pds->stats[i].recommended_compression = PC_DIM_ZLIB;
		/* Only use rle and sigbits compression on integer values */
		/* If we can do better than 4:1 we might beat zlib */
		if ( dim->interpretation != PC_DOUBLE )
		{
			/* If sigbits is better than 4:1, use that */
			if ( raw_size/sigbits_size > 1.6 )
			{
				pds->stats[i].recommended_compression = PC_DIM_SIGBITS;
			}
			/* If RLE size is even better, use that. */
			if ( raw_size/rle_size > 4.0 )
			{
				pds->stats[i].recommended_compression = PC_DIM_RLE;
			}
		}
	}
	return PC_SUCCESS;
}

/**
 * @param a dimstat (singular! ) to clone, inlcuding new memory allocation
 * @return return a copy of the input dimstat, on own memory
 * */
PCDIMSTAT * 
pc_dimstat_clone( PCDIMSTAT * input_dimstat)
{
	PCDIMSTAT *pds;
	pds = pcalloc(sizeof(PCDIMSTAT));
	
	pds->total_runs = input_dimstat->total_runs ;
	pds->total_commonbits = input_dimstat->total_commonbits;
	pds->recommended_compression = input_dimstat->recommended_compression;
	
	return pds;
}

/** 
 * @param the dimstats we want to partially clone
 * @param an array containng the position of the dimension we want to keep
 * @param the total number of dimensions we want to keep
 * @return a new dimstats object with only stats for dimension in array, stats are newly numbered foloowing the index of the dimension in the array
 * */
PCDIMSTATS * 
pc_dimstats_clone_subset(const PCDIMSTATS * d, uint32_t * dimensions_position_array, uint32_t dimensions_number)
{
	//Allocating new memory for the clone
	PCDIMSTATS *pds;
	int i;
	pds = pcalloc(sizeof(PCDIMSTATS));
	pds->ndims = dimensions_number;
	pds->stats = pcalloc(pds->ndims * sizeof(PCDIMSTAT));
	
	//loop on stat to copy content
	for ( i = 0; i < dimensions_number; i++ )
	{
		if ( sizeof(d->stats[dimensions_position_array[i]]) != 0L)
		{
				printf("\n dimension position array : %d \n",dimensions_position_array[i]);
				
				((*pds).stats[i]) = *pc_dimstat_clone( &(d->stats[dimensions_position_array[i]]) ); 
				
			printf("cloning the stat %d in new position %d",dimensions_position_array[i],i);
		}
	}
	
	return pds;
}


