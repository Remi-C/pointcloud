/*******************************
*Remi-C
* 
*adding a new patch_explode to pointcloud, retunring only a subset of dimensions of patch
*
*
**********************************/

--test env :
--importing function
DROP FUNCTION IF EXISTS pc_patchsubset(p pcpatch, dimensions text[]);
CREATE OR REPLACE FUNCTION pc_patchsubset(p pcpatch, dimensions TEXT[])
  RETURNS pcpatch AS
'$libdir/pointcloud', 'pcpatch_subset'
  LANGUAGE c IMMUTABLE STRICT;

 

   DROP FUNCTION IF EXISTS pc_explode_reducedim(p pcpatch, dimensions text[]);
CREATE OR REPLACE FUNCTION pc_explode_reducedim(p pcpatch, dimensions text[])
  RETURNS SETOF double precision[] AS
'$libdir/pointcloud', 'pcpatch_unnest_reduce_dimension'
  LANGUAGE c IMMUTABLE STRICT

   DROP FUNCTION IF EXISTS pc_get_all(p pcpoint);
CREATE OR REPLACE FUNCTION pc_get_all(p pcpoint)
  RETURNS SETOF double precision[] AS
'$libdir/pointcloud', 'pcpoint_get_all_values'
  LANGUAGE c IMMUTABLE STRICT;


DROP FUNCTION IF EXISTS pc_get(pt pcpoint, dimnames text[]);
  CREATE OR REPLACE FUNCTION pc_get(pt pcpoint, dimnames text[])
  RETURNS double precision[] AS
'$libdir/pointcloud', 'pcpoint_get_values'
  LANGUAGE c IMMUTABLE STRICT;

  DROP FUNCTION IF EXISTS pc_get(pt pcpoint, dimnames text[]);
  CREATE OR REPLACE FUNCTION pc_get(pt pcpoint, dimnames text[])
  RETURNS double precision[] AS
'$libdir/pointcloud', 'pcpoint_get_values'
  LANGUAGE c IMMUTABLE STRICT;
 
 
 --inserting schema
 /*
 INSERT INTO pointcloud_formats (pcid, srid, schema) VALUES (1, 4326, 
'<?xml version="1.0" encoding="UTF-8"?>
<pc:PointCloudSchema xmlns:pc="http://pointcloud.org/schemas/PC/1.1" 
    xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
  <pc:dimension>
    <pc:position>1</pc:position>
    <pc:size>4</pc:size>
    <pc:description>X coordinate as a long integer. You must use the 
                    scale and offset information of the header to 
                    determine the double value.</pc:description>
    <pc:name>X</pc:name>
    <pc:interpretation>int32_t</pc:interpretation>
    <pc:scale>0.01</pc:scale>
  </pc:dimension>
  <pc:dimension>
    <pc:position>2</pc:position>
    <pc:size>4</pc:size>
    <pc:description>Y coordinate as a long integer. You must use the 
                    scale and offset information of the header to 
                    determine the double value.</pc:description>
    <pc:name>Y</pc:name>
    <pc:interpretation>int32_t</pc:interpretation>
    <pc:scale>0.01</pc:scale>
  </pc:dimension>
  <pc:dimension>
    <pc:position>3</pc:position>
    <pc:size>4</pc:size>
    <pc:description>Z coordinate as a long integer. You must use the 
                    scale and offset information of the header to 
                    determine the double value.</pc:description>
    <pc:name>Z</pc:name>
    <pc:interpretation>int32_t</pc:interpretation>
    <pc:scale>0.01</pc:scale>
  </pc:dimension>
  <pc:dimension>
    <pc:position>4</pc:position>
    <pc:size>2</pc:size>
    <pc:description>The intensity value is the integer representation 
                    of the pulse return magnitude. This value is optional 
                    and system specific. However, it should always be 
                    included if available.</pc:description>
    <pc:name>Intensity</pc:name>
    <pc:interpretation>uint16_t</pc:interpretation>
    <pc:scale>1</pc:scale>
  </pc:dimension>
  <pc:metadata>
    <Metadata name="compression">dimensional</Metadata>
  </pc:metadata>
</pc:PointCloudSchema>');
*/

--fill with test data 
/*
  CREATE TABLE points (
    id SERIAL PRIMARY KEY,
    pt PCPOINT(1)
);

-- A table of patches
CREATE TABLE patches (
    id SERIAL PRIMARY KEY,
    pa PCPATCH(1)
);

INSERT INTO points (pt)
SELECT PC_MakePoint(1, ARRAY[x,y,z,intensity])
FROM (
  SELECT  
  -127+a/100.0 AS x, 
    45+a/100.0 AS y,
         1.0*a AS z,
          a/10 AS intensity
  FROM generate_series(1,100) AS a
) AS values;


INSERT INTO patches (pa)
SELECT PC_Patch(pt) FROM points GROUP BY id/10;

*/

--checking test data
SELECT id, PC_AsText(pa)
FROM patches
LIMIT 1 ;

--testing function 
SELECT id 
FROM patches AS  pa, pc_patchsubset(pa, ARRAY['Y'::text,'X'::text,'Z']) AS result
LIMIT 1 ;


 
SELECT id, result
FROM patches AS  pa, pc_explode_reducedim(pa, ARRAY['x' ,'Y' ,'Z','Intensity','z' ]) AS result
WHERE id=2 

SELECT p[1],p[2],p[3],p[4]
FROM 
(
SELECT pc_get_all(result) As p
FROM  
 (SELECT id, result
FROM patches AS  pa, pc_explode(pa) AS result
WHERE id=1
 ) as toto
) AS titi


 
SELECT pc_get(result,ARRAY['Yer' ]) As p
FROM  
 (SELECT id, result
FROM patches AS  pa, pc_explode(pa) AS result
WHERE id=1
 ) as toto
 


SELECT result[1],result[2]
FROM  
 (SELECT id, result
FROM patches AS  pa, pc_explode_reducedim(pa, ARRAY['Z'::text,'Y'::text]) AS result
WHERE id=1
 ) as toto


SELECT *
FROM UNNEST(array[1,2,3,4])


SELECT id 
FROM patches AS  pa, pc_patchsubset(pa,  ARRAY['X '::text,'Y'::text] ) AS result
LIMIT 1 ;

SELECT id ,pc_patchsubset(pa) AS result
FROM patches AS  pa 
LIMIT 1 ;
 
{"pcid":1,"pts":[[2.68567e+06,45.8,-126.2,8000],[2.68567e+06,45.81,-126.19,0],[2.68567e+06,45.82,-126.18,8100],[2.68567e+06,45.83,-126.17,0],[2.68567e+06,45.84,-126.16,8200],[2.68567e+06,45.85,-126.15,0],[2.68567e+06,45.86,-126.14,8300],[2.68567e+06,45.87,-126.13,0],[2.68567e+06,45.88,-126.12,8400],[2.68567e+06,45.89,-126.11,0]]}

