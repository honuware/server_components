# Phase 3.1.6: endpoint_test_helper — the reusable endpoint test harness (builds a
# WebApp with the framework test doubles and collects routes) — moved here from
# src/endpoints/. It is app-free (the app-side test main anchors
# Endpoints::RegisterAllEndpoints) and ships in honuware_testing. Kept under an
# endpoints/ subdir so its include prefix stays "endpoints/endpoint_test_helper.h".
target_sources(honuware_testing
    PRIVATE
    endpoint_test_helper.h
    endpoint_test_helper.cpp
)
