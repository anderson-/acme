ifdef ACME_DIR
  MKDIR := $(ACME_DIR)
else ifneq (,$(wildcard ./acme))
  MKDIR := ./acme
else ifneq (,$(wildcard ../acme))
  MKDIR := ../acme
else
  $(error ACME not found. Set ACME_DIR or place acme in ./acme or ../acme)
endif

SRC := $(PWD)

ifneq (,$(wildcard $(PWD)/wifi.yaml))
  WIFI := $(PWD)/wifi.yaml
endif

-include $(MKDIR)/makefile