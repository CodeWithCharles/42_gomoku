#!/usr/bin/make -f
# ---------------------------------------------------------------------------- #
#                                  Log macros                                  #
# ---------------------------------------------------------------------------- #

BUILD_COLOR = \033[1;32m
RM_COLOR    = \033[1;31m
OK_COLOR    = \033[1;32m
ERR_COLOR   = \033[1;31m
NO_COLOR    = \033[0m

define \n


endef

# arg1 msg affiche, arg2 arg affiche, arg3 commande, arg4 couleur
define cmd
	$(if $(MAKE_FULL_LOG),, \
@printf "$(NO_COLOR)""$4""$1""$(NO_COLOR)"" ""$(NO_COLOR)""$2""$(NO_COLOR)""\n")
	$(if $(MAKE_FULL_LOG),,@)$3
endef

define qcmd
	$(if $(MAKE_FULL_LOG),,@)$1
endef

define msg
	$(if $(MAKE_FULL_LOG),, \
@printf "$2""> ""$(NO_COLOR)""$1""$(NO_COLOR)""\n")
endef

define omsg
	$(call msg,$1,$(OK_COLOR))
endef

define bcmd
	$(call cmd,$1,$2,$3,$(BUILD_COLOR))
endef

define rmsg
	$(call msg,$1,$(RM_COLOR))
endef

define emsg
	$(call msg,$1,$(ERR_COLOR))
endef