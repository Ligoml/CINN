core_gather_headers()

gather_srcs(cinnapi_src SRCS
        gather_nd.cc
        scatter.cc
        flip.cc
        sort.cc
        argmin.cc
        argmax.cc
        repeat.cc
        lookup_table.cc
        one_hot.cc
        cbrt.cc
        clz.cc
        popc.cc
        reciprocal.cc
        logical_right_shift.cc
        )

cc_test(test_gather_nd SRCS gather_nd_test.cc DEPS cinncore)
cc_test(test_scatter SRCS scatter_test.cc DEPS cinncore)
cc_test(test_sort SRCS sort_test.cc DEPS cinncore)
cc_test(test_argmin SRCS argmin_test.cc DEPS cinncore)
cc_test(test_argmax SRCS argmax_test.cc DEPS cinncore)
cc_test(test_flip SRCS flip_test.cc DEPS cinncore)
cc_test(test_repeat SRCS repeat_test.cc DEPS cinncore)
cc_test(test_one_hot SRCS one_hot_test.cc DEPS cinncore)
cc_test(test_lookup_table SRCS lookup_table_test.cc DEPS cinncore)
cc_test(test_cbrt SRCS cbrt_test.cc DEPS cinncore)
cc_test(test_clz SRCS clz_test.cc DEPS cinncore)
cc_test(test_popc SRCS popc_test.cc DEPS cinncore)
cc_test(test_reciprocal SRCS reciprocal_test.cc DEPS cinncore)
cc_test(test_logical_right_shift SRCS logical_right_shift_test.cc DEPS cinncore)
