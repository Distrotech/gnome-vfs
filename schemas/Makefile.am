schemadir   = $(GCONF_SCHEMA_FILE_DIR)
schema_DATA = \
	system_http_proxy.schemas \
	desktop_default_applications.schemas

# don't do this if we are building in eg. rpm
if GCONF_SCHEMAS_INSTALL
install-data-local:
	if test -z "$(DESTDIR)" ; then \
		for p in $(schema_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(srcdir)/$$p; \
		done \
	fi
else
install-data-local:
endif

EXTRA_DIST = $(schema_DATA)
