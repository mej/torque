include $(top_srcdir)/buildutils/config.mk

DIST_SUBDIRS =

CHECK_DIR = test

CLEANFILES = *.gcda *.gcno *.gcov

include_HEADERS = attr_node_func.h

noinst_LIBRARIES = libattr.a

libattr_a_SOURCES = attr_atomic.c attr_fn_acl.c attr_fn_arst.c attr_fn_b.c \
		     attr_fn_c.c attr_fn_hold.c attr_fn_intr.c attr_fn_l.c \
		     attr_fn_ll.c attr_fn_nppcu.c attr_fn_resc.c attr_fn_size.c \
		     attr_fn_str.c attr_fn_time.c attr_fn_unkn.c \
		     attr_func.c attr_node_func.c attr_fn_tokens.c attr_fn_tv.c \
                     attr_str_conversion.c attr_fn_freq.c 

if HAVE_CHECK
check:
	$(MAKE) -C $(CHECK_DIR) $(MAKECMDGOALS)

.PHONY: cleancheck
cleancheck:
	cd test && $(MAKE) cleancheck
endif
