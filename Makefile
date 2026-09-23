# Ce Makefile ne declare que des cibles executables en l'etat. Les cibles du
# sujet ($(NAME), all, clean, fclean, re, test, bench, debug) viendront avec
# src/ : une cible vide serait pire que son absence.
#
# La cible par defaut est help tant que all n'existe pas : lancer un npm install
# sur un simple `make` serait une surprise desagreable.
#
# help se construit tout seul en relisant ce fichier : la premiere ligne du
# commentaire pose au-dessus d'une cible lui sert de description. Il n'y a donc
# rien a tenir a jour en double.

PORT ?= 8642
MOCK  = tools/mock-engine.mjs

.DEFAULT_GOAL := help

# Liste les cibles disponibles, decrites par leur propre commentaire.
help:
	@echo "Gomoku - cibles disponibles"
	@echo ""
	@awk ' \
	  /^# / { if (doc == "") doc = substr($$0, 3); next } \
	  /^[a-zA-Z_-]+:/ { \
	    if (doc != "") { \
	      name = $$1; sub(/:.*/, "", name); \
	      printf "  make %-6s %s\n", name, doc; \
	    } \
	  } \
	  { doc = "" } \
	' $(MAKEFILE_LIST)
	@echo ""
	@echo "Variable : PORT=$(PORT)   (exemple : make mock PORT=9000)"

# Compile l'interface React dans ui/dist (necessite node).
ui:
	@cd ui && npm install && npm run build

# Ouvre la fenetre Electron sur le moteur factice.
app: ui
	@cd app && npm install && npm run start:mock

# Sert ui/dist via le moteur factice, pour un navigateur ordinaire.
mock: ui
	@node $(MOCK) --no-browser --port $(PORT) --ui ui/dist

# Lance le moteur factice et le serveur Vite, avec le proxy /ws.
# Le moteur ne meurt pas avec Vite : pkill -f mock-engine apres coup.
dev:
	@cd ui && npm install
	@node $(MOCK) --no-browser --port $(PORT) & cd ui && npm run dev

.PHONY: help ui app mock dev
