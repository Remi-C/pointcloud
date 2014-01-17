
   DROP FUNCTION IF EXISTS pc_explode_reducedim(p pcpatch, dimensions text[]);
CREATE OR REPLACE FUNCTION pc_explode_reducedim(p pcpatch, dimensions text[])
  RETURNS SETOF double precision[] AS
'$libdir/pointcloud', 'pcpatch_unnest_reduce_dimension'
  LANGUAGE c IMMUTABLE STRICT

--benchmarking :
--outputing points into filesystem using classical output



 
COPY 
(WITH patches AS (
	SELECT p.gid, p.patch
	FROM acquisition_tmob_012013.riegl_pcpatch_space AS p, def_zone_test
	WHERE ST_Intersects(p.patch::geometry,geom) = TRUE
	),
points AS (
SELECT PC_Explode(patches.patch ) AS point
FROM patches
)
SELECT PC_Get(point,'x'),pc_get(point,'y'),pc_get(point,'z'),pc_get(point, 'GPS_time'),pc_get(point, 'reflectance')
-- SELECT ROUND(PC_Get(point,'x'),3) , ROUND(pc_get(point,'y'),3), ROUND(pc_get(point,'z'),3), ROUND(pc_get(point, 'GPS_time'),6), ROUND(pc_get(point, 'reflectance'),3)
	FROM points
	LIMIT 2000000 )
    TO '/tmp/bench_pointcloud' 
--31sec text : 25 sec 45 Mo
--binaire : 25sec 84 Mo
--pour 2M points et sans round : 43.4sec


COPY  (
WITH patches AS (
	SELECT p.gid, p.patch
	FROM acquisition_tmob_012013.riegl_pcpatch_space AS p, def_zone_test
	WHERE ST_Intersects(p.patch::geometry,geom) = TRUE
	),
points AS (
SELECT pc_explode_reducedim(patch, ARRAY['x','y','z','GPS_time','reflectance']) AS point
FROM patches
)
--SELECT point
--SELECT ROUND(point[1]::numeric,3) , ROUND(point[2]::numeric,3), ROUND(point[3]::numeric,3), ROUND(point[4]::numeric,6), ROUND(point[5]::numeric,3)
SELECT  point[1]  ,  point[2] ,  point[3] ,  point[4] ,  point[5] 
	FROM points
	LIMIT 2000000 )
	    TO '/tmp/bench_pointcloud'  
--21 sec text, 45 Mo
--26 sec binarire, 85 Mo
--36 sans round et pour 2M points


