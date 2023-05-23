set(EG_CPP_SRC
    egadsCopy.cpp 
    egadsGeom.cpp
    egadsHLevel.cpp
    egadsIO.cpp 
    egadsSkinning.cpp 
    egadsSpline.cpp 
    egadsSplineFit.cpp 
    egadsTessSens.cpp 
    egadsTopo.cpp 
    OCC/BRepLib_FuseEdges.cpp
    OCC/BRepPrimAPI_MakePrism.cpp
    OCC/BRepPrimAPI_MakeRevol.cpp 
    OCC/BRepSweep_Prism.cpp 
    OCC/BRepSweep_Revol.cpp 
    OCC/BRepSweep_Rotation.cpp 
    OCC/BRepSweep_Translation.cpp
    ../util/evaluate.c
    ../util/rational.c 
)

set(EG_C_SRC  
    egadsAttrs.c 
    egadsBase.c 
    egadsEffect.c
    egadsExport.c 
    egadsFit.c 
    egadsMemory.c 
    egadsQuads.c 
    egadsRobust.c 
    egadsSBO.c 
    egadsSolids.c 
    egadsTess.c 
    egadsTessInp.c 
    egadsTris.c 
    prmCfit.c 
    prmGrid.c 
    prmUV.c 
    ../util/emp.c
    ../util/regQuads.c
    ../util/retessFaces.c
    ../util/egadsUVmap.c
)
set_source_files_properties(../util/evaluate.c PROPERTIES LANGUAGE CXX)
set_source_files_properties(../util/rational.c PROPERTIES LANGUAGE CXX)

file(GLOB UVMAP_SRC "../util/uvmap/*.c")

# todo(philip): create object library with all egads source, then create shared/static libraries
add_library(egads SHARED ${EG_CPP_SRC} ${EG_C_SRC} ${UVMAP_SRC})
add_library(egads_static STATIC ${EG_CPP_SRC} ${EG_C_SRC} ${UVMAP_SRC})
link_directories(${OCC_LIB_DIR})
target_link_libraries(egads PRIVATE ${OCC_LIB_NAMES})
target_link_libraries(egads_static ${OCC_LIBRARIES})
if (UNIX AND NOT APPLE)
	target_link_options(egads PUBLIC "-Wl,--disable-new-dtags,-rpath,${OCC_EXTRACT_DIR}/lib/")
endif()
set_target_properties(egads PROPERTIES INSTALL_RPATH "\$ORIGIN/opencascade/")

add_definitions("-w")

