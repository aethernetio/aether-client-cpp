# Install script for directory: /home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set path to fallback-tool for dependency-resolution.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Devel" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/man/man3" TYPE FILE FILES
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_cancel.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_create_query.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_destroy.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_destroy_options.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_class_fromstr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_class_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_class_tostr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_datatype_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_flags_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_mapping.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_opcode_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_opcode_tostr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_opt_datatype_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_opt_get_datatype.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_opt_get_name.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_parse.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rcode_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rcode_tostr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rec_type_fromstr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rec_type_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rec_type_tostr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_create.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_destroy.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_duplicate.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_get_flags.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_get_id.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_get_opcode.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_get_rcode.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_query_add.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_query_cnt.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_query_get.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_query_set_name.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_query_set_type.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_rr_add.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_rr_cnt.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_rr_del.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_rr_get.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_rr_get_const.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_record_set_id.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_add_abin.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_del_abin.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_del_opt_byid.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_abin.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_abin_cnt.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_addr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_addr6.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_bin.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_class.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_keys.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_name.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_opt.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_opt_byid.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_opt_cnt.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_str.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_ttl.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_type.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_u16.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_u32.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_get_u8.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_key_datatype.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_key_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_key_to_rec_type.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_key_tostr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_set_addr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_set_addr6.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_set_bin.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_set_opt.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_set_str.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_set_u16.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_set_u32.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_rr_set_u8.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_section_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_section_tostr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dns_write.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_dup.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_expand_name.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_expand_string.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_fds.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_free_data.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_free_hostent.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_free_string.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_freeaddrinfo.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_get_servers.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_get_servers_csv.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_get_servers_ports.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_getaddrinfo.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_gethostbyaddr.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_gethostbyname.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_gethostbyname_file.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_getnameinfo.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_getsock.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_inet_ntop.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_inet_pton.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_init.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_init_options.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_library_cleanup.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_library_init.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_library_init_android.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_library_initialized.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_mkquery.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_opt_param_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_a_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_aaaa_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_caa_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_mx_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_naptr_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_ns_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_ptr_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_soa_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_srv_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_txt_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_parse_uri_reply.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_process.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_process_fd.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_process_fds.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_process_pending_write.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_query.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_query_dnsrec.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_queue.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_queue_active_queries.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_queue_wait_empty.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_reinit.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_save_options.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_search.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_search_dnsrec.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_send.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_send_dnsrec.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_local_dev.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_local_ip4.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_local_ip6.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_pending_write_cb.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_query_enqueue_cb.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_server_state_callback.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_servers.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_servers_csv.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_servers_ports.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_servers_ports_csv.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_socket_callback.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_socket_configure_callback.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_socket_functions.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_socket_functions_ex.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_set_sortlist.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_strerror.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_svcb_param_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_threadsafety.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_timeout.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_tlsa_match_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_tlsa_selector_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_tlsa_usage_t.3"
    "/home/angel/projects/aether-client-cpp/build-test/cpm.cache/c-ares/0fc8/docs/ares_version.3"
    )
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "/home/angel/projects/aether-client-cpp/build-test/_deps/c-ares-build/docs/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
