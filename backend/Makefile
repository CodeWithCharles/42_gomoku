#!/usr/bin/make -f
# ---------------------------------------------------------------------------- #
#                                    Gomoku                                    #
#                                                                              #
#  Le sujet impose : un executable nomme `Gomoku`, les regles $(NAME), all,    #
#  clean, fclean, re, et l'absence de relink.                                  #
#                                                                              #
#  Ce Makefile est autonome : ni mise, ni npm, ni Electron. Un correcteur      #
#  clone le depot, tape `make`, ca compile.                                    #
# ---------------------------------------------------------------------------- #
all: all-gomoku

include Makefile.vars ../Makefile.msg

# -------------------------------- Main build -------------------------------- #
all-gomoku: $(NAME_PATH)

$(NAME_PATH): $(OBJS_CORE) $(OBJS_C) $(OBJS_MAIN)
	$(call qcmd,$(MKDIR) -p $(@D))
	$(call bcmd,ld,$(NAME),$(LD) -o $@ $(OBJS_CORE) $(OBJS_C) $(OBJS_MAIN) $(LDFLAGS))
	$(call omsg,$(NAME) [$(PROFILE)])

$(OBJDIR)/%.cpp.o: %.cpp
	$(call qcmd,$(MKDIR) -p $(@D))
	$(call bcmd,cxx,$<,$(CXX) -c $(CXXFLAGS) -o $@ $<)

$(OBJDIR)/%.c.o: %.c
	$(call qcmd,$(MKDIR) -p $(@D))
	$(call bcmd,cc,$<,$(CC) -c $(CFLAGS) -o $@ $<)

# ------------------------------- Checks config ------------------------------ #
# Absent d'un clone frais : on le genere au lieu d'echouer, pour qu'un simple
# `make` suffise. make relance ensuite automatiquement la lecture des includes.
Makefile.cfg:
	$(call omsg,No Makefile.cfg - running ./configure.sh with defaults)
	$(call qcmd,./configure.sh >/dev/null)

# ---------------------------------- Cleanup --------------------------------- #
clean:
	$(call rmsg,Deleting objects ($(OBJDIR)))
	$(call qcmd,$(RM) -rf $(OBJDIR))

fclean: clean
	$(call rmsg,Deleting $(NAME)$(,) $(TEST_NAME) and $(BENCH_NAME))
	$(call qcmd,$(RM) -f $(NAME_PATH) $(TEST_PATH) $(BENCH_PATH))

mrproper: fclean
	$(call rmsg,Deleting Makefile.cfg)
	$(call qcmd,$(RM) -f Makefile.cfg)

re: fclean all

# ----------------------------------- Tests ---------------------------------- #
$(TEST_PATH): $(OBJS_CORE) $(OBJS_C) $(OBJS_TEST)
	$(call qcmd,$(MKDIR) -p $(@D))
	$(call bcmd,ld,$(TEST_NAME),$(LD) -o $@ $(OBJS_CORE) $(OBJS_C) $(OBJS_TEST) $(LDFLAGS))

test: $(TEST_PATH)
	$(call qcmd,$(TEST_PATH))

# ----------------------------------- Bench ---------------------------------- #
$(BENCH_PATH): $(OBJS_CORE) $(OBJS_C) $(OBJS_BENCH)
	$(call qcmd,$(MKDIR) -p $(@D))
	$(call bcmd,ld,$(BENCH_NAME),$(LD) -o $@ $(OBJS_CORE) $(OBJS_C) $(OBJS_BENCH) $(LDFLAGS))

bench: $(BENCH_PATH)
	$(call qcmd,$(BENCH_PATH))

# ------------------------------ Build profiles ------------------------------ #
# C'est configure.sh qui detient l'etat du build, et il passe par mrproper :
# impossible de se retrouver avec un binaire instrumente qu'on croit release.
# Le prix a payer est que changer de profil recompile tout.
debug:
	$(call omsg,Reconfiguring for debug - this wipes the current build)
	$(call qcmd,./configure.sh --debug --sanitize --optimize-disable --native-disable)
	$(call qcmd,$(MAKE) --no-print-directory all)

release:
	$(call omsg,Reconfiguring for release - this wipes the current build)
	$(call qcmd,./configure.sh)
	$(call qcmd,$(MAKE) --no-print-directory all)

# ---------------------------------- Format ---------------------------------- #
format:
	$(call qcmd,test -z "$(FMT_FILES)" || $(CLANG_FORMAT) -i $(FMT_FILES))
	$(call omsg,sources formatted)

format-check:
	$(call qcmd,test -z "$(FMT_FILES)" || $(CLANG_FORMAT) --dry-run -Werror $(FMT_FILES))
	$(call omsg,formatting is clean)

# ----------------------------------- Front ---------------------------------- #
# Jamais une dependance de `all` : ui/dist est versionne (ADR-006).
ui:
	$(call qcmd,test -d ui || { printf 'ui/ does not exist yet\n' >&2; exit 1; })
	$(call qcmd,cd ui && npm install && npm run build)

app: all
	$(call qcmd,test -d app || { printf 'app/ does not exist yet\n' >&2; exit 1; })
	$(call qcmd,cd app && npm install && npm start)

-include $(DEPS)

.PHONY: all all-gomoku clean fclean mrproper re test bench debug release \
format format-check ui app