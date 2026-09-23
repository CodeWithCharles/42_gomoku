#!/usr/bin/make -f
# ---------------------------------------------------------------------------- #
#                                    Gomoku                                    #
#  Orchestrateur. Le moteur C++ vit dans backend/, le front dans ui/ et app/.  #
# ---------------------------------------------------------------------------- #

# Son Makefile visait help tant que `all` n'existait pas. Maintenant que le
# moteur est la, un correcteur qui tape `make` doit obtenir un binaire.
.DEFAULT_GOAL := all

include Makefile.msg

# ---------------------------------- Variables ------------------------------- #
BACKEND			:= backend
PORT			?= 8642
MOCK			:= tools/mock-engine.mjs

ENGINE			:= $(MAKE) --no-print-directory -C $(BACKEND)

# --------------------------------- Moteur C++ ------------------------------- #

# Compile le moteur C++ et depose ./Gomoku a la racine.
all:
	$(call qcmd,$(ENGINE) all)

# Supprime les objets du moteur.
clean:
	$(call qcmd,$(ENGINE) clean)

# Supprime les objets et les binaires du moteur.
fclean:
	$(call qcmd,$(ENGINE) fclean)

# Supprime tout, y compris la configuration generee du moteur.
mrproper:
	$(call qcmd,$(ENGINE) mrproper)

# Recompile le moteur de zero.
re:
	$(call qcmd,$(ENGINE) re)

# Lance la batterie de tests des regles.
test:
	$(call qcmd,$(ENGINE) test)

# Mesure la profondeur atteinte et les noeuds par seconde.
bench:
	$(call qcmd,$(ENGINE) bench)

# Reconfigure le moteur en debug (ASan + UBSan) et le recompile.
debug:
	$(call qcmd,$(ENGINE) debug)

# Reconfigure le moteur en release et le recompile.
release:
	$(call qcmd,$(ENGINE) release)

# Reformate les sources C++.
format:
	$(call qcmd,$(ENGINE) format)

# Echoue si une source C++ n'est pas formatee.
format-check:
	$(call qcmd,$(ENGINE) format-check)

# ----------------------------------- Front ---------------------------------- #

# Compile l'interface React dans ui/dist (necessite node).
ui:
	$(call omsg,Building ui/dist)
	$(call qcmd,cd ui && npm install && npm run build)

# Ouvre la fenetre Electron sur le moteur factice.
app: ui
	$(call omsg,Starting Electron shell)
	$(call qcmd,cd app && npm install && npm run start:mock)

# Sert ui/dist via le moteur factice, pour un navigateur ordinaire.
mock: ui
	$(call omsg,Mock engine on port $(PORT))
	$(call qcmd,node $(MOCK) --no-browser --port $(PORT) --ui ui/dist)

# Lance le moteur factice et le serveur Vite, avec le proxy /ws.
# Le moteur ne meurt pas avec Vite : pkill -f mock-engine apres coup.
dev:
	$(call qcmd,cd ui && npm install)
	$(call omsg,Mock engine on port $(PORT) + Vite dev server)
	$(call qcmd,node $(MOCK) --no-browser --port $(PORT) & cd ui && npm run dev)

# ------------------------------------ Aide ---------------------------------- #

# Liste les cibles disponibles, decrites par leur propre commentaire.
help:
	@echo "Gomoku - cibles disponibles"
	@echo ""
	@awk ' \
	  /^# / { if (doc == "") doc = substr($$0, 3); next } \
	  /^[a-zA-Z_-]+:/ { \
	    if (doc != "") { \
	      name = $$1; sub(/:.*/, "", name); \
	      printf "  make %-12s %s\n", name, doc; \
	    } \
	  } \
	  { doc = "" } \
	' $(MAKEFILE_LIST)
	@echo ""
	@echo "Variable : PORT=$(PORT)   (exemple : make mock PORT=9000)"

.PHONY: all clean fclean mrproper re test bench debug release \
format format-check ui app mock dev help