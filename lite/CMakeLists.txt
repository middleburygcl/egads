set(LITE_SRC  
    liteAttrs.c
    liteBase.c
    #liteDevice.c 
    liteGeom.c 
    liteImport.c 
    liteMemory.c 
    liteString.c 
    liteTopo.c 
    liteUVmap.c
    ../util/emp.c
    ../util/evaluate.c
    ../util/rational.c
    ../util/regQuads.c
    #../util/retessFaces.c
    #../util/egadsUVmap.c
    ../src/egadsRobust.c
    ../src/egadsEffect.c
    ../src/egadsTess.c
    ../src/egadsTris.c
    ../src/egadsQuads.c
    ../src/egadsTessInp.c
)


include_directories(.)
add_library(egadslite SHARED ${LITE_SRC})
add_definitions("-w")
add_definitions("-DLITE")
