# honuware_layering.cmake — component layer enforcement.
#
# Makes the component layer diagram an executable, build-time invariant. The
# honuware componentization stacks targets in fixed layers; a target may only
# link targets BELOW it. CMake, left to its own devices, happily permits cycles
# between STATIC libraries and never complains about an upward edge — so without
# this module the layering is only a convention.
#
# Two mechanisms, used together:
#
#   honuware_add_component(NAME <c> [DEPENDS <lower components...>])
#       Declares a layered component: creates its library target and, if DEPENDS
#       is given, validates each declared component dependency against the DAG
#       (FATAL on an upward/sideways edge) and links it. DEPENDS only needs to
#       list COMPONENT edges; Conan/imported libs are linked as usual in each
#       component's own CMakeLists (co-located with its sources). Most components
#       declare their inter-component edge in their own CMakeLists next to their
#       Conan deps, so callers here typically create the target with no DEPENDS
#       and rely on the final validation pass below.
#
#   honuware_validate_layering()
#       Called ONCE after every add_subdirectory has run. Reads each component's
#       ACTUAL resolved LINK_LIBRARIES and re-checks every inter-component edge
#       against the DAG. This is the loophole-closer: it catches an illegal edge
#       no matter which CMakeLists declared it (the per-call DEPENDS check only
#       sees edges routed through honuware_add_component), so a back-edge added
#       anywhere fails configuration.
#
# The DAG below is the single source of truth for the layer diagram. Changing an
# allow-list is the deliberate act of changing the architecture — review it.
#
# NOTE: this is the framework-only variant that ships in the honuware component
# repo. The consuming application (e.g. knottyyoga) keeps its own superset of
# this DAG that adds its app targets on top (knotty_yoga_core -> testing ->
# tests). honuware_validate_layering() skips any component not present in a given
# build, so the two variants coexist.

# The functions below use the if(... IN_LIST ...) operator, which requires
# CMP0057=NEW. Set it here so the module is self-contained and usable even from a
# context (e.g. cmake -P script mode) that hasn't already set it — CMake captures
# a function's policy state at DEFINITION time, so this governs the functions
# regardless of the caller's policy stack.
cmake_policy(SET CMP0057 NEW)

# --- The layer DAG -----------------------------------------------------------
# For each layered target, the FULL set of other layered targets it MAY link
# (its own dependencies, direct or transitive). Anything not listed is a
# violation. We list the full downward set (not just direct edges) so the check
# is a simple subset test independent of how callers group their links.
#
# Layer order (low -> high): foundation -> data -> services -> platform ->
#   testing -> tests. honuware_square is a side branch on foundation ONLY (a
#   generic Square client with no data/secrets coupling), so platform
#   deliberately may NOT link it — only a consuming app's payment logic may.
set(HONUWARE_ALLOW_honuware_foundation  "")
set(HONUWARE_ALLOW_honuware_data        honuware_foundation)
set(HONUWARE_ALLOW_honuware_services    honuware_data honuware_foundation)
set(HONUWARE_ALLOW_honuware_square      honuware_foundation)
set(HONUWARE_ALLOW_honuware_platform    honuware_services honuware_data honuware_foundation)
set(HONUWARE_ALLOW_honuware_testing     honuware_platform honuware_services honuware_data honuware_square honuware_foundation)
set(HONUWARE_ALLOW_honuware_tests       honuware_testing honuware_platform honuware_services honuware_data honuware_square honuware_foundation)

# The complete set of DAG-governed targets. Anything NOT in this list (Conan
# imported targets, leaf executables) is ignored by the checks.
set(HONUWARE_COMPONENTS
    honuware_foundation
    honuware_data
    honuware_services
    honuware_square
    honuware_platform
    honuware_testing
    honuware_tests)

# --- Internal: check a target's declared/resolved deps against its allow-list --
function(_honuware_check_component_deps name deps)
    if(NOT DEFINED HONUWARE_ALLOW_${name})
        message(FATAL_ERROR
            "honuware layering: '${name}' has no allow-list. Add it to "
            "HONUWARE_COMPONENTS with an explicit HONUWARE_ALLOW_${name} in "
            "cmake/honuware_layering.cmake.")
    endif()
    set(allowed ${HONUWARE_ALLOW_${name}})
    foreach(dep IN LISTS deps)
        # Only inter-component edges are governed; ignore Conan/imported targets
        # and any non-component library name.
        if(dep IN_LIST HONUWARE_COMPONENTS)
            if(NOT dep IN_LIST allowed)
                message(FATAL_ERROR
                    "\n==== honuware layering violation ====\n"
                    "  '${name}' may not depend on '${dep}'.\n"
                    "  '${name}' is only permitted to link: ${allowed}\n"
                    "  This edge would break the honuware component layer DAG "
                    "(and, on GNU ld, likely a static-library cycle). Either "
                    "remove the dependency, or — if the layer diagram is "
                    "genuinely changing — update HONUWARE_ALLOW_${name} in "
                    "cmake/honuware_layering.cmake and reconsider the extraction "
                    "order.\n"
                    "=====================================")
            endif()
        endif()
    endforeach()
endfunction()

# --- Declare a layered component (create target; validate+link DEPENDS) -------
function(honuware_add_component)
    cmake_parse_arguments(HAC "" "NAME" "DEPENDS" ${ARGN})
    if(NOT HAC_NAME)
        message(FATAL_ERROR "honuware_add_component: NAME <component> is required")
    endif()
    if(NOT DEFINED HONUWARE_ALLOW_${HAC_NAME})
        message(FATAL_ERROR
            "honuware_add_component: '${HAC_NAME}' is not a declared honuware "
            "layered component. Add it to HONUWARE_COMPONENTS and give it an "
            "explicit allow-list in cmake/honuware_layering.cmake first.")
    endif()

    add_library(${HAC_NAME} "")

    if(HAC_DEPENDS)
        _honuware_check_component_deps(${HAC_NAME} "${HAC_DEPENDS}")
        target_link_libraries(${HAC_NAME} PUBLIC ${HAC_DEPENDS})
    endif()
endfunction()

# --- Validate the fully-resolved graph (run last) -----------------------------
function(honuware_validate_layering)
    foreach(component IN LISTS HONUWARE_COMPONENTS)
        if(NOT TARGET ${component})
            # A component may legitimately not exist in a partial/standalone
            # build; skip it rather than fail.
            continue()
        endif()
        get_target_property(deps ${component} LINK_LIBRARIES)
        if(NOT deps)
            continue()
        endif()
        _honuware_check_component_deps(${component} "${deps}")
    endforeach()
    message(STATUS
        "honuware: component layer DAG validated across "
        "[${HONUWARE_COMPONENTS}]")
endfunction()
