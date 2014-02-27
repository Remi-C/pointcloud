CREATE OR REPLACE FUNCTION pc_patchsubset(p pcpatch)
    RETURNS pcpatch AS 'MODULE_PATHNAME', 'pcpatch_subset'
    LANGUAGE 'c' IMMUTABLE STRICT;   
