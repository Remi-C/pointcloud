/***********************************************************************
* pc_access.c
*
*  Accessor/aggregate functions for points and patches in PgSQL.
*
*  PgSQL Pointcloud is free and open source software provided
*  by the Government of Canada
*  Copyright (c) 2013 Natural Resources Canada
*
***********************************************************************/

#include "pc_pgsql.h"      /* Common PgSQL support for our type */
#include "utils/numeric.h"
#include "funcapi.h"

/* General SQL functions */
Datum pcpoint_get_value(PG_FUNCTION_ARGS);
Datum pcpatch_from_pcpoint_array(PG_FUNCTION_ARGS);
Datum pcpatch_from_pcpatch_array(PG_FUNCTION_ARGS);
Datum pcpatch_uncompress(PG_FUNCTION_ARGS);
Datum pcpatch_subset(PG_FUNCTION_ARGS);
Datum pcpatch_numpoints(PG_FUNCTION_ARGS);
Datum pcpatch_compression(PG_FUNCTION_ARGS);
Datum pcpatch_intersects(PG_FUNCTION_ARGS);
Datum pcpatch_get_stat(PG_FUNCTION_ARGS);
Datum pcpatch_filter(PG_FUNCTION_ARGS);
Datum pcpatch_size(PG_FUNCTION_ARGS);
Datum pcpoint_size(PG_FUNCTION_ARGS);
Datum pc_version(PG_FUNCTION_ARGS);

/* Generic aggregation functions */
Datum pointcloud_agg_transfn(PG_FUNCTION_ARGS);
Datum pointcloud_abs_in(PG_FUNCTION_ARGS);
Datum pointcloud_abs_out(PG_FUNCTION_ARGS);

/* Point finalizers */
Datum pcpoint_agg_final_pcpatch(PG_FUNCTION_ARGS);
Datum pcpoint_agg_final_array(PG_FUNCTION_ARGS);

/* Patch finalizers */
Datum pcpatch_agg_final_array(PG_FUNCTION_ARGS);
Datum pcpatch_agg_final_pcpatch(PG_FUNCTION_ARGS);

/* Deaggregation functions */
Datum pcpatch_unnest(PG_FUNCTION_ARGS);
Datum pcpatch_unnest_reduce_dimension(PG_FUNCTION_ARGS);

/**
* Read a named dimension from a PCPOINT
* PC_Get(point pcpoint, dimname text) returns Numeric
*/
PG_FUNCTION_INFO_V1(pcpoint_get_value);
Datum pcpoint_get_value(PG_FUNCTION_ARGS)
{
	SERIALIZED_POINT *serpt = PG_GETARG_SERPOINT_P(0);
	text *dim_name = PG_GETARG_TEXT_P(1);
	char *dim_str;
	float8 double_result;

	PCSCHEMA *schema = pc_schema_from_pcid(serpt->pcid, fcinfo);
	PCPOINT *pt = pc_point_deserialize(serpt, schema);
	if ( ! pt )
		PG_RETURN_NULL();

	dim_str = text_to_cstring(dim_name);
	if ( ! pc_point_get_double_by_name(pt, dim_str, &double_result) )
	{
		pc_point_free(pt);
		elog(ERROR, "dimension \"%s\" does not exist in schema", dim_str);
	}
	pfree(dim_str);
	pc_point_free(pt);
	PG_RETURN_DATUM(DirectFunctionCall1(float8_numeric, Float8GetDatum(double_result)));
}

static inline bool
array_get_isnull(const bits8 *nullbitmap, int offset)
{
	if (nullbitmap == NULL)
	{
		return false; /* assume not null */
	}
	if (nullbitmap[offset / 8] & (1 << (offset % 8)))
	{
		return false; /* not null */
	}
	return true;
}

static PCPATCH *
pcpatch_from_point_array(ArrayType *array, FunctionCallInfoData *fcinfo)
{
	int nelems;
	bits8 *bitmap;
	size_t offset = 0;
	int i;
	uint32 pcid = 0;
	PCPATCH *pa;
	PCPOINTLIST *pl;
	PCSCHEMA *schema = 0;

	/* How many things in our array? */
	nelems = ArrayGetNItems(ARR_NDIM(array), ARR_DIMS(array));

	/* PgSQL supplies a bitmap of which array entries are null */
	bitmap = ARR_NULLBITMAP(array);

	/* Empty array? Null return */
	if ( nelems == 0 )
		return NULL;

	/* Make our holder */
	pl = pc_pointlist_make(nelems);

	offset = 0;
	bitmap = ARR_NULLBITMAP(array);
	for ( i = 0; i < nelems; i++ )
	{
		/* Only work on non-NULL entries in the array */
		if ( ! array_get_isnull(bitmap, i) )
		{
			SERIALIZED_POINT *serpt = (SERIALIZED_POINT *)(ARR_DATA_PTR(array)+offset);
			PCPOINT *pt;

			if ( ! schema )
			{
				schema = pc_schema_from_pcid(serpt->pcid, fcinfo);
			}

			if ( ! pcid )
			{
				pcid = serpt->pcid;
			}
			else if ( pcid != serpt->pcid )
			{
				elog(ERROR, "pcpatch_from_point_array: pcid mismatch (%d != %d)", serpt->pcid, pcid);
			}

			pt = pc_point_deserialize(serpt, schema);
			if ( ! pt )
			{
				elog(ERROR, "pcpatch_from_point_array: point deserialization failed");
			}

			pc_pointlist_add_point(pl, pt);

			offset += INTALIGN(VARSIZE(serpt));
		}

	}

	if ( pl->npoints == 0 )
		return NULL;

	pa = pc_patch_from_pointlist(pl);
	pc_pointlist_free(pl);
	return pa;
}


static PCPATCH *
pcpatch_from_patch_array(ArrayType *array, FunctionCallInfoData *fcinfo)
{
	int nelems;
	bits8 *bitmap;
	size_t offset = 0;
	int i;
	uint32 pcid = 0;
	PCPATCH *pa;
	PCPATCH **palist;
	int numpatches = 0;
	PCSCHEMA *schema = 0;

	/* How many things in our array? */
	nelems = ArrayGetNItems(ARR_NDIM(array), ARR_DIMS(array));

	/* PgSQL supplies a bitmap of which array entries are null */
	bitmap = ARR_NULLBITMAP(array);

	/* Empty array? Null return */
	if ( nelems == 0 )
		return NULL;

	/* Make our temporary list of patches */
	palist = pcalloc(nelems*sizeof(PCPATCH*));

	/* Read the patches out of the array and deserialize */
	offset = 0;
	bitmap = ARR_NULLBITMAP(array);
	for ( i = 0; i < nelems; i++ )
	{
		/* Only work on non-NULL entries in the array */
		if ( ! array_get_isnull(bitmap, i) )
		{
			SERIALIZED_PATCH *serpatch = (SERIALIZED_PATCH *)(ARR_DATA_PTR(array)+offset);

			if ( ! schema )
			{
				schema = pc_schema_from_pcid(serpatch->pcid, fcinfo);
			}

			if ( ! pcid )
			{
				pcid = serpatch->pcid;
			}
			else if ( pcid != serpatch->pcid )
			{
				elog(ERROR, "pcpatch_from_patch_array: pcid mismatch (%d != %d)", serpatch->pcid, pcid);
			}

			pa = pc_patch_deserialize(serpatch, schema);
			if ( ! pa )
			{
				elog(ERROR, "pcpatch_from_patch_array: patch deserialization failed");
			}

			palist[numpatches++] = pa;

			offset += INTALIGN(VARSIZE(serpatch));
		}

	}

	/* Can't do anything w/ NULL */
	if ( numpatches == 0 )
		return NULL;

	/* Pass to the lib to build the output patch from the list */
	pa = pc_patch_from_patchlist(palist, numpatches);

	/* Free the temporary patch list */
	for ( i = 0; i < numpatches; i++ )
	{
		pc_patch_free(palist[i]);
	}
	pcfree(palist);

	return pa;
}


PG_FUNCTION_INFO_V1(pcpatch_from_pcpatch_array);
Datum pcpatch_from_pcpatch_array(PG_FUNCTION_ARGS)
{
	ArrayType *array;
	PCPATCH *pa;
	SERIALIZED_PATCH *serpa;

	if ( PG_ARGISNULL(0) )
		PG_RETURN_NULL();

	array = DatumGetArrayTypeP(PG_GETARG_DATUM(0));
	pa = pcpatch_from_patch_array(array, fcinfo);
	if ( ! pa )
		PG_RETURN_NULL();

	serpa = pc_patch_serialize(pa, NULL);
	pc_patch_free(pa);
	PG_RETURN_POINTER(serpa);
}

PG_FUNCTION_INFO_V1(pcpatch_from_pcpoint_array);
Datum pcpatch_from_pcpoint_array(PG_FUNCTION_ARGS)
{
	ArrayType *array;
	PCPATCH *pa;
	SERIALIZED_PATCH *serpa;

	if ( PG_ARGISNULL(0) )
		PG_RETURN_NULL();

	array = DatumGetArrayTypeP(PG_GETARG_DATUM(0));
	pa = pcpatch_from_point_array(array, fcinfo);
	if ( ! pa )
		PG_RETURN_NULL();

	serpa = pc_patch_serialize(pa, NULL);
	pc_patch_free(pa);
	PG_RETURN_POINTER(serpa);
}

typedef struct
{
	ArrayBuildState *s;
} abs_trans;

PG_FUNCTION_INFO_V1(pointcloud_abs_in);
Datum pointcloud_abs_in(PG_FUNCTION_ARGS)
{
	ereport(ERROR,(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
	               errmsg("function pointcloud_abs_in not implemented")));
	PG_RETURN_POINTER(NULL);
}

PG_FUNCTION_INFO_V1(pointcloud_abs_out);
Datum pointcloud_abs_out(PG_FUNCTION_ARGS)
{
	ereport(ERROR,(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
	               errmsg("function pointcloud_abs_out not implemented")));
	PG_RETURN_POINTER(NULL);
}


PG_FUNCTION_INFO_V1(pointcloud_agg_transfn);
Datum pointcloud_agg_transfn(PG_FUNCTION_ARGS)
{
	Oid arg1_typeid = get_fn_expr_argtype(fcinfo->flinfo, 1);
	MemoryContext aggcontext;
	abs_trans *a;
	ArrayBuildState *state;
	Datum elem;

	if (arg1_typeid == InvalidOid)
		ereport(ERROR,
		        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
		         errmsg("could not determine input data type")));

	if (fcinfo->context && IsA(fcinfo->context, AggState))
	{
		aggcontext = ((AggState *) fcinfo->context)->aggcontext;
	}
	else if (fcinfo->context && IsA(fcinfo->context, WindowAggState))
	{
		aggcontext = ((WindowAggState *) fcinfo->context)->aggcontext;
	}
	else
	{
		/* cannot be called directly because of dummy-type argument */
		elog(ERROR, "pointcloud_agg_transfn called in non-aggregate context");
		aggcontext = NULL;  /* keep compiler quiet */
	}

	if ( PG_ARGISNULL(0) )
	{
		a = (abs_trans*) palloc(sizeof(abs_trans));
		a->s = NULL;
	}
	else
	{
		a = (abs_trans*) PG_GETARG_POINTER(0);
	}
	state = a->s;
	elem = PG_ARGISNULL(1) ? (Datum) 0 : PG_GETARG_DATUM(1);
	state = accumArrayResult(state,
	                         elem,
	                         PG_ARGISNULL(1),
	                         arg1_typeid,
	                         aggcontext);
	a->s = state;

	PG_RETURN_POINTER(a);
}




static Datum
pointcloud_agg_final(abs_trans *a, MemoryContext mctx, FunctionCallInfo fcinfo)
{
	ArrayBuildState *state;
	int dims[1];
	int lbs[1];
	state = a->s;
	dims[0] = state->nelems;
	lbs[0] = 1;
	return makeMdArrayResult(state, 1, dims, lbs, mctx, false);
}

PG_FUNCTION_INFO_V1(pcpoint_agg_final_array);
Datum pcpoint_agg_final_array(PG_FUNCTION_ARGS)
{
	abs_trans *a;
	Datum result = 0;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();   /* returns null iff no input values */

	a = (abs_trans*) PG_GETARG_POINTER(0);

	result = pointcloud_agg_final(a, CurrentMemoryContext, fcinfo);
	PG_RETURN_DATUM(result);
}


PG_FUNCTION_INFO_V1(pcpatch_agg_final_array);
Datum pcpatch_agg_final_array(PG_FUNCTION_ARGS)
{
	abs_trans *a;
	Datum result = 0;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();   /* returns null iff no input values */

	a = (abs_trans*) PG_GETARG_POINTER(0);

	result = pointcloud_agg_final(a, CurrentMemoryContext, fcinfo);
	PG_RETURN_DATUM(result);
}


PG_FUNCTION_INFO_V1(pcpoint_agg_final_pcpatch);
Datum pcpoint_agg_final_pcpatch(PG_FUNCTION_ARGS)
{
	ArrayType *array;
	abs_trans *a;
	PCPATCH *pa;
	SERIALIZED_PATCH *serpa;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();   /* returns null iff no input values */

	a = (abs_trans*) PG_GETARG_POINTER(0);

	array = DatumGetArrayTypeP(pointcloud_agg_final(a, CurrentMemoryContext, fcinfo));
	pa = pcpatch_from_point_array(array, fcinfo);
	if ( ! pa )
		PG_RETURN_NULL();

	serpa = pc_patch_serialize(pa, NULL);
	pc_patch_free(pa);
	PG_RETURN_POINTER(serpa);
}


PG_FUNCTION_INFO_V1(pcpatch_agg_final_pcpatch);
Datum pcpatch_agg_final_pcpatch(PG_FUNCTION_ARGS)
{
	ArrayType *array;
	abs_trans *a;
	PCPATCH *pa;
	SERIALIZED_PATCH *serpa;

	if (PG_ARGISNULL(0))
		PG_RETURN_NULL();   /* returns null iff no input values */

	a = (abs_trans*) PG_GETARG_POINTER(0);

	array = DatumGetArrayTypeP(pointcloud_agg_final(a, CurrentMemoryContext, fcinfo));
	pa = pcpatch_from_patch_array(array, fcinfo);
	if ( ! pa )
		PG_RETURN_NULL();

	serpa = pc_patch_serialize(pa, NULL);
	pc_patch_free(pa);
	PG_RETURN_POINTER(serpa);
}


PG_FUNCTION_INFO_V1(pcpatch_unnest);
Datum pcpatch_unnest(PG_FUNCTION_ARGS)
{
	typedef struct
	{
		int nextelem;
		int numelems;
		PCPOINTLIST *pointlist;
	} pcpatch_unnest_fctx;

	FuncCallContext *funcctx;
	pcpatch_unnest_fctx *fctx;
	MemoryContext oldcontext;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		PCPATCH *patch;
		SERIALIZED_PATCH *serpatch;

		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		* switch to memory context appropriate for multiple function calls
		*/
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/*
		* Get the patch value and detoast if needed.  We can't do this
		* earlier because if we have to detoast, we want the detoasted copy
		* to be in multi_call_memory_ctx, so it will go away when we're done
		* and not before.      (If no detoast happens, we assume the originally
		* passed array will stick around till then.)
		*/
		serpatch = PG_GETARG_SERPATCH_P(0);
		patch = pc_patch_deserialize(serpatch, pc_schema_from_pcid_uncached(serpatch->pcid));

		/* allocate memory for user context */
		fctx = (pcpatch_unnest_fctx *) palloc(sizeof(pcpatch_unnest_fctx));

		/* initialize state */
		fctx->nextelem = 0;
		fctx->numelems = patch->npoints;
		fctx->pointlist = pc_pointlist_from_patch(patch);

		/* save user context, switch back to function context */
		funcctx->user_fctx = fctx;
		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
	funcctx = SRF_PERCALL_SETUP();
	fctx = funcctx->user_fctx;

	if (fctx->nextelem < fctx->numelems)
	{
		Datum elem;
		PCPOINT *pt = pc_pointlist_get_point(fctx->pointlist, fctx->nextelem);
		SERIALIZED_POINT *serpt = pc_point_serialize(pt);
		fctx->nextelem++;
		elem = PointerGetDatum(serpt);
		SRF_RETURN_NEXT(funcctx, elem);
	}
	else
	{
		/* do when there is no more left */
		SRF_RETURN_DONE(funcctx);
	}
}


PG_FUNCTION_INFO_V1(pcpatch_unnest_reduce_dimension);
Datum pcpatch_unnest_reduce_dimension(PG_FUNCTION_ARGS)
{
	typedef struct
	{
		int nextelem;
		int numelems;
		PCPOINTLIST *pointlist;
		PCSCHEMA * reduced_schema;
	} pcpatch_unnest_reduce_dimension_fctx;

	FuncCallContext *funcctx;
	pcpatch_unnest_reduce_dimension_fctx *fctx;
	MemoryContext oldcontext;

	/* stuff done only on the first call of the function */
	if (SRF_IS_FIRSTCALL())
	{
		PCPATCH *patch;
		PCPATCH *patch_output;
		SERIALIZED_PATCH *serpatch;
		PCSCHEMA * schema;
		int i;
		int j ;
		int ndim;
		char ** final_dimension_array;
		Datum temp_text_array_datum;
		ndim=0;

		
		
				
		/* create a function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/*
		* switch to memory context appropriate for multiple function calls
		*/
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
		
		/*
		* Get the patch value and detoast if needed.  We can't do this
		* earlier because if we have to detoast, we want the detoasted copy
		* to be in multi_call_memory_ctx, so it will go away when we're done
		* and not before.      (If no detoast happens, we assume the originally
		* passed array will stick around till then.)
		*/
		//getting the serialized patch
				//pcinfo("getting serpatch\n");
		serpatch = PG_GETARG_SERPATCH_P(0);
		funcctx->max_calls = serpatch->npoints; //setting the max number of call : one per point to output
		pc_serpatch_to_string(serpatch,NULL);
		//getting the schema
				//pcinfo("getting schema\n");
		//schema = pc_schema_from_pcid(serpatch->pcid, fcinfo); //doesn't work because we need to writte the schema in the cache
		schema = pc_schema_from_pcid_uncached(serpatch->pcid);
		//getting the text[] that represent the dimensions we want to keep
				//pcinfo("getting text[] content\n");
		temp_text_array_datum = PG_GETARG_DATUM(1);
		final_dimension_array = pccstringarray_from_Datum(temp_text_array_datum,&ndim);
			for(i=0;i<ndim;i++)
					{
						if((j=pc_schema_get_dimension_position_by_name(schema, final_dimension_array[i])) == -1 )
							{pcerror("error, you asked to keep the dimension  ░▒▓%s▓▒░, yet this dimension doesn't exist in the schema %s\n"
								,final_dimension_array[i],pc_schema_to_json(schema));
							}
						//pcinfo(" the dimension %s has position %d and exists \n",final_dimension_array[i],j);
					}
		
		//deserializing
				//pcinfo("deserializing\n");
		patch = pc_patch_deserialize(serpatch, schema);
		
		//reducing the number of dimension
				//pcinfo("reducing number of dims\n");
		patch_output = pc_patch_reduce_dimension(patch,final_dimension_array,ndim);
		
		//updating schema with new schema (containing less dimensions)
				//pcinfo("updating schema\n");
		schema = pc_schema_clone(patch_output->schema);
		
		
		//freeing unneeded patch :
				//pcinfo("freeing\n");
		pcfree(patch);

		/* allocate memory for user context */
				//pcinfo("allocating mem\n");
		fctx = (pcpatch_unnest_reduce_dimension_fctx *) palloc(sizeof(pcpatch_unnest_reduce_dimension_fctx));

		/* initialize state */
				//pcinfo("initialize stat\n");
		fctx->nextelem = 0;
		fctx->numelems = patch_output->npoints;
				//pcinfo("pcpointlist from reduced patch \n");
				//pcinfo("patch we want to extract point of : \n%s",pc_patch_to_string(patch_output));
		fctx->reduced_schema = schema; //saving the reduced schema
		fctx->pointlist = pc_pointlist_from_patch(patch_output);

		/* save user context, switch back to function context */
				//pcinfo("saving user context\n");
		funcctx->user_fctx = fctx;
		MemoryContextSwitchTo(oldcontext);
	}

	/* stuff done on every call of the function */
			//pcinfo("every call of unnest_reduce_dimension: switching function\n");
	funcctx = SRF_PERCALL_SETUP();
	fctx = funcctx->user_fctx;

	if (fctx->nextelem < fctx->numelems)
	{
		//pcinfo("another point to output\n");
		
		Datum * transdatums = (Datum * )  pcalloc(fctx->reduced_schema->ndims * sizeof(Datum) ) ;
        ArrayType  *result;
        int i;
        double temp_double;
        PCPOINT * pt = pc_pointlist_get_point(fctx->pointlist, fctx->nextelem);
        for(i=0;i<fctx->reduced_schema->ndims;i++) {
			pc_point_get_double_by_index(pt,i, &temp_double);
					//pcinfo("putting the double %f in memory",temp_double);
			//transdatums[i]=  Float8GetDatumFast(temp_double);
			transdatums[i]=  Float8GetDatum(temp_double);
		}
        
        result = construct_array(transdatums, fctx->reduced_schema->ndims,
                                 FLOAT8OID,
                                 sizeof(float8), FLOAT8PASSBYVAL, 'd');
        //PG_RETURN_ARRAYTYPE_P(result);
        
        fctx->nextelem++;
		//SRF_RETURN_NEXT(funcctx, *transdatums);
		SRF_RETURN_NEXT(funcctx, PointerGetDatum(result));
		
		/* //this wont work: need ot output an array of float
		Datum elem;
		PCPOINT *pt = pc_pointlist_get_point(fctx->pointlist, fctx->nextelem);
		SERIALIZED_POINT *serpt = pc_point_serialize(pt);
		fctx->nextelem++;
		elem = PointerGetDatum(serpt);
		SRF_RETURN_NEXT(funcctx, elem);
		* */
	}
	else
	{
		//pcinfo("end of function\n");
		/* do when there is no more left */
		SRF_RETURN_DONE(funcctx);
	}
}


PG_FUNCTION_INFO_V1(pcpatch_uncompress);
Datum pcpatch_uncompress(PG_FUNCTION_ARGS)
{
	SERIALIZED_PATCH *serpa = PG_GETARG_SERPATCH_P(0);
	PCSCHEMA *schema = pc_schema_from_pcid(serpa->pcid, fcinfo);
	PCPATCH *patch = pc_patch_deserialize(serpa, schema);
	SERIALIZED_PATCH *serpa_out = pc_patch_serialize_to_uncompressed(patch);
	pc_patch_free(patch);
	PG_RETURN_POINTER(serpa_out);
}

//added by Remi-C @CHECK @TEST @WARNING @DEV
PG_FUNCTION_INFO_V1(pcpatch_subset);
Datum pcpatch_subset(PG_FUNCTION_ARGS)
{
		PCPATCH *patch_output;
		SERIALIZED_PATCH *serpa_out;
		SERIALIZED_PATCH *serpa;
		PCSCHEMA *schema ;
		PCPATCH *patch; 
		//uint32_t new_dim_number = 3; 
		//char * dim_to_keep[4] = {"x","y","Z","intensity"}; 

		int ndim;
		char ** final_dimension_array;
		int i;
		int j;
		ndim=0;
		j=-1;
		
		
		//get input patch and deserialize it
		serpa = PG_GETARG_SERPATCH_P(0);
		schema = pc_schema_from_pcid(serpa->pcid, fcinfo);
		patch = pc_patch_deserialize( serpa,schema);
		
		//get input text[], convert it to an array of cstring, check that the asked dimensions exist in the schema.
			//get input text[] and convert it to cstring
					//pcinfo("trying to retrieve text argument \n "); 
				final_dimension_array = pccstringarray_from_Datum(PG_GETARG_DATUM(1),&ndim);
			
			//check that the asked dimensions exist in the schema
					//pcinfo("checking that %d input dimension names exist in schema \n",ndim);
				for(i=0;i<ndim;i++)
				{
					if((j=pc_schema_get_dimension_position_by_name(schema, final_dimension_array[i])) == -1 )
						{pcerror("error, you asked to keep the dimension  ░▒▓%s▓▒░, yet this dimension doesn't exist in the schema %s\n"
							,final_dimension_array[i],pc_schema_to_json(schema));
						}
					//pcinfo(" the dimension %s has position %d and exists \n",final_dimension_array[i],j);
				}
				
		//reduce the patch dimension without decompressing it 
				//pcinfo("reducing the dimensionnality of the patch");
			patch_output = pc_patch_reduce_dimension(patch,final_dimension_array,ndim);
			schema = patch_output->schema;
			
		//printing the result
				pcinfo("the reduced dimnesionnality patch : %s",pc_patch_to_string(patch_output));
		
		//serialization
				//pcinfo("serializing the reduced patch");
			serpa_out = pc_patch_serialize(patch_output, NULL);
			
		//clean up
				//pcinfo("freeing");
			pc_patch_free(patch);
			pc_patch_free(patch_output);
			
		//returning result
				//pcinfo("returning pointer to serpatch");
			PG_RETURN_POINTER(serpa_out);
		
		
		//debug print 	
				//pcinfo("size of the returned data : %zu",pc_patch_serialized_size( patch_output));
				//printing some info about the output serpatch
				//pc_serpatch_to_string(serpa_out, schema);
				//	pcinfo("patch we want to reduce : %s",pc_patch_to_string(patch));
				//	pcinfo(" the original PCBYTES array :%s",pc_patch_dimensional_bytes_array_to_string((PCPATCH_DIMENSIONAL*)patch));
				//pcerror("patch we want to reduce : %s",pc_patch_to_string(patch));
					//pcinfo("\n the modified PCBYTES array :%s \n",pc_patch_dimensional_bytes_array_to_string((PCPATCH_DIMENSIONAL*)patch_output));
					//computig dimstats
					//pds = pc_dimstats_make(patch_output->schema);
					//pc_dimstats_update(pds,(PCPATCH_DIMENSIONAL *) patch_output );
				//pcinfo("size of the returned data : %zu",pc_patch_serialized_size( patch_output));
				//printing some info about the output serpatch
				//pc_serpatch_to_string(serpa_out, schema);

}

PG_FUNCTION_INFO_V1(pcpatch_numpoints);
Datum pcpatch_numpoints(PG_FUNCTION_ARGS)
{
	SERIALIZED_PATCH *serpa = PG_GETHEADER_SERPATCH_P(0);
	PG_RETURN_INT32(serpa->npoints);
}

PG_FUNCTION_INFO_V1(pcpatch_compression);
Datum pcpatch_compression(PG_FUNCTION_ARGS)
{
	SERIALIZED_PATCH *serpa = PG_GETHEADER_SERPATCH_P(0);
	PG_RETURN_INT32(serpa->compression);
}

PG_FUNCTION_INFO_V1(pcpatch_intersects);
Datum pcpatch_intersects(PG_FUNCTION_ARGS)
{
	SERIALIZED_PATCH *serpa1 = PG_GETHEADER_SERPATCH_P(0);
	SERIALIZED_PATCH *serpa2 = PG_GETHEADER_SERPATCH_P(1);

	if ( serpa1->pcid != serpa2->pcid )
		elog(ERROR, "%s: pcid mismatch (%d != %d)", __func__, serpa1->pcid, serpa2->pcid);

	if ( pc_bounds_intersects(&(serpa1->bounds), &(serpa2->bounds)) )
	{
		PG_RETURN_BOOL(TRUE);
	}
	PG_RETURN_BOOL(FALSE);
}

PG_FUNCTION_INFO_V1(pcpatch_size);
Datum pcpatch_size(PG_FUNCTION_ARGS)
{
	SERIALIZED_PATCH *serpa = PG_GETARG_SERPATCH_P(0);
	PG_RETURN_INT32(VARSIZE(serpa));
}

PG_FUNCTION_INFO_V1(pcpoint_size);
Datum pcpoint_size(PG_FUNCTION_ARGS)
{
	SERIALIZED_POINT *serpt = PG_GETARG_SERPOINT_P(0);
	PG_RETURN_INT32(VARSIZE(serpt));
}

PG_FUNCTION_INFO_V1(pc_version);
Datum pc_version(PG_FUNCTION_ARGS)
{
	text *version_text;
	char version[64];
	snprintf(version, 64, "%s", POINTCLOUD_VERSION);
	version_text = cstring_to_text(version);
	PG_RETURN_TEXT_P(version_text);
}

/**
* Read a named dimension statistic from a PCPATCH
* PC_PatchMax(patch pcpatch, dimname text) returns Numeric
* PC_PatchMin(patch pcpatch, dimname text) returns Numeric
* PC_PatchAvg(patch pcpatch, dimname text) returns Numeric
*/
PG_FUNCTION_INFO_V1(pcpatch_get_stat);
Datum pcpatch_get_stat(PG_FUNCTION_ARGS)
{
	static int stats_size_guess = 400;
	SERIALIZED_PATCH *serpa = PG_GETHEADERX_SERPATCH_P(0, stats_size_guess);
	PCSCHEMA *schema = pc_schema_from_pcid(serpa->pcid, fcinfo);
	char *dim_str = text_to_cstring(PG_GETARG_TEXT_P(1));
	char *stat_str = text_to_cstring(PG_GETARG_TEXT_P(2));
	PCSTATS *stats;
	float8 double_result;
	int rv = 0;

	if ( stats_size_guess < pc_stats_size(schema) )
	{
		serpa = PG_GETHEADERX_SERPATCH_P(0, pc_stats_size(schema) );
	}

	stats = pc_patch_stats_deserialize(schema, serpa->data);

	if ( ! stats )
		PG_RETURN_NULL();

	/* Max */
	if ( 0 == strcasecmp("max", stat_str) )
		rv = pc_point_get_double_by_name(&(stats->max), dim_str, &double_result);
	/* Min */
	else if ( 0 == strcasecmp("min", stat_str) )
		rv = pc_point_get_double_by_name(&(stats->min), dim_str, &double_result);
	/* Avg */
	else if ( 0 == strcasecmp("avg", stat_str) )
		rv = pc_point_get_double_by_name(&(stats->avg), dim_str, &double_result);
	/* Unsupported */
	else
		elog(ERROR, "stat type \"%s\" is not supported", stat_str);

	pfree(stat_str);
	pc_stats_free(stats);

	if ( ! rv )
		elog(ERROR, "dimension \"%s\" does not exist in schema", dim_str);

	pfree(dim_str);
	PG_RETURN_DATUM(DirectFunctionCall1(float8_numeric, Float8GetDatum(double_result)));
}


/**
* PC_FilterLessThan(patch pcpatch, dimname text, value) returns PcPatch
* PC_FilterGreaterThan(patch pcpatch, dimname text, value) returns PcPatch
* PC_FilterEqual(patch pcpatch, dimname text, value) returns PcPatch
* PC_FilterBetween(patch pcpatch, dimname text, value1, value2) returns PcPatch
*/
PG_FUNCTION_INFO_V1(pcpatch_filter);
Datum pcpatch_filter(PG_FUNCTION_ARGS)
{
	SERIALIZED_PATCH *serpatch = PG_GETARG_SERPATCH_P(0);
	PCSCHEMA *schema = pc_schema_from_pcid(serpatch->pcid, fcinfo);
	char *dim_name = text_to_cstring(PG_GETARG_TEXT_P(1));
	float8 value1 = PG_GETARG_FLOAT8(2);
	float8 value2 = PG_GETARG_FLOAT8(3);
	int32 mode = PG_GETARG_INT32(4);
	PCPATCH *patch;
	PCPATCH *patch_filtered = NULL;
	SERIALIZED_PATCH *serpatch_filtered;

	patch = pc_patch_deserialize(serpatch, schema);
	if ( ! patch )
	{
		elog(ERROR, "failed to deserialize patch");
		PG_RETURN_NULL();
	}

	switch ( mode )
	{
	case 0:
		patch_filtered = pc_patch_filter_lt_by_name(patch, dim_name, value1);
		break;
	case 1:
		patch_filtered = pc_patch_filter_gt_by_name(patch, dim_name, value1);
		break;
	case 2:
		patch_filtered = pc_patch_filter_equal_by_name(patch, dim_name, value1);
		break;
	case 3:
		patch_filtered = pc_patch_filter_between_by_name(patch, dim_name, value1, value2);
		break;
	default:
		elog(ERROR, "unknown mode \"%d\"", mode);
	}

	pc_patch_free(patch);
	PG_FREE_IF_COPY(serpatch, 0);

	if ( ! patch_filtered )
	{
		elog(ERROR, "dimension \"%s\" does not exist", dim_name);
	}
	pfree(dim_name);

    /* Always treat zero-point patches as SQL NULL */
	if ( patch_filtered->npoints <= 0 )
	{
		pc_patch_free(patch_filtered);
		PG_RETURN_NULL();
	}

	serpatch_filtered = pc_patch_serialize(patch_filtered, NULL);
	pc_patch_free(patch_filtered);

	PG_RETURN_POINTER(serpatch_filtered);
}



