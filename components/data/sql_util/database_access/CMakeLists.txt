target_sources(honuware_data
    PRIVATE
    database_crud_helpers.h
    database_crud_helpers.cpp
    database_helper.h
    database_helper.cpp
    database_helper_base.h    
    database_helper_init.h
    database_helper_init.cpp
    database_metadata.h
    database_metadata.cpp
    database_util.h
    database_util.cpp
    db_and_table_operations.h
    db_and_table_operations.cpp
    extensions.h
    extensions.cpp
    production_transaction_provider.h
    production_transaction_provider.cpp
    transaction.h
    transaction_impl.h
    transaction_impl.cpp
    transaction_provider.h
)

target_sources(${HONUWARE_TESTS_TARGET}
    PUBLIC
    database_crud_helpers_priv.h
    database_crud_helpers_test.cpp
    database_helper_init_test.cpp
    database_metadata_test.cpp
    database_util_test.cpp
    db_and_table_operations_test.cpp
    extensions_test.cpp
    transaction_impl_test.cpp
)

target_link_libraries(honuware_data PUBLIC ${BOOST_LIB} ${CURL_LIB} ${CROW_LIB} ${PQXX_LIB})

target_link_libraries(${HONUWARE_TESTS_TARGET} PUBLIC  ${PQXX_LIB} ${GTEST_LIB})