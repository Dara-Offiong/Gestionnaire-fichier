/*
 * Fichier : gestionnaire.c
 * Auteurs :
 *  - MEZIANE Fella - 33.33%
 *  - BOUKILI Inas - 33.33%
  * - OFFIONG Dara - 33.33%
 * Date de création : 15/02/2025
 * Dernière modification : 05/04/2025
 */
#include "gestionnaire.h"

// Définitions des variables globales
FileSystem backups[MAX_BACKUPS];
time_t backup_times[MAX_BACKUPS];
int backup_count = 0;
FileSystem fs;
int initialized = 0;
FileDescriptor open_files[MAX_FILES];
static int handle_parent_dir(void);


char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup) memcpy(dup, s, len);
    return dup;
}

static inline int min(int a, int b) {
    return a < b ? a : b;
}

void log_operation(const char *op, int inode, int block) {
    if (DEBUG) {
        time_t now = time(NULL);
        printf("[%s] Inode:%d Bloc:%d à %s", 
               op, inode, block, ctime(&now));
    }
}
void verify_fs() {
    // Vérification des blocs
    int actual_free_blocks = 0;
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (fs.free_blocks[i] == 0) actual_free_blocks++;
    }
    
    if (actual_free_blocks != fs.free_block_count) {
        fprintf(stderr, "CORRECTION: Incohérence blocs (compteur:%d vs réel:%d)\n",
               fs.free_block_count, actual_free_blocks);
        fs.free_block_count = actual_free_blocks;
    }
    
    // Vérification des inodes
    int actual_free_inodes = 0;
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] == 0) actual_free_inodes++;
    }
    
    if (actual_free_inodes != fs.free_inode_count) {
        fprintf(stderr, "CORRECTION: Incohérence inodes (compteur:%d vs réel:%d)\n",
               fs.free_inode_count, actual_free_inodes);
        fs.free_inode_count = actual_free_inodes;
    }
    
    // Vérification des tailles
    if (fs.free_block_count < 0 || fs.free_block_count > MAX_BLOCKS ||
        fs.free_inode_count < 0 || fs.free_inode_count > MAX_FILES) {
        fprintf(stderr, "ERREUR CRITIQUE: Compteurs invalides! Réinitialisation...\n");
        format_fs();
    }
}

// Calcule le taux de fragmentation du système de fichiers.
int calculate_fragmentation() {
    int used_blocks = 0;
    int gaps = 0;
    int last_used = -1;

    // Compter les blocs utilisés et les "trous"
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (fs.free_blocks[i] == 1) {
            used_blocks++;
            if (last_used != -1 && i != last_used + 1) {
                gaps++;
            }
            last_used = i;
        }
    }

    if (used_blocks <= 1) return 0; // Pas de fragmentation si 0 ou 1 bloc utilisé
    return (gaps * 100) / (used_blocks - 1);
}
/**
 * Défragmentation automatique si le taux dépasse le seuil (30%)
 */
// Lance une défragmentation automatique si nécessaire.
 void auto_defragment_if_needed() {
    if (fs.free_block_count == MAX_BLOCKS) return; // Aucun bloc utilisé

    int fragmentation = calculate_fragmentation();
    if (DEBUG) {
        printf("[DEFRAG] Taux de fragmentation: %d%%\n", fragmentation);
    }

    // Seuil à 30% - ajustable selon vos besoins
    if (fragmentation > 30) {
        printf("\033[1;33m[DEFRAG] Défragmentation automatique déclenchée (taux: %d%%)\033[0m\n", fragmentation);
        defragment_light();
    }
}

// Initialise le système de fichiers en mémoire.
void init_fs() {
    // Vérifier si déjà initialisé
    if (initialized) {
        if (DEBUG) printf("[DEBUG] Système déjà initialisé\n");
        return;
    }

    // Initialisation de la structure principale
    memset(&fs, 0, sizeof(fs));
    
    // 1. Initialisation des blocs (tous libres)
    for (int i = 0; i < MAX_BLOCKS; i++) {
        fs.free_blocks[i] = 0;  // 0 = bloc libre
    }
    fs.free_block_count = MAX_BLOCKS;
    
    // 2. Initialisation des inodes (libres sauf racine)
    for (int i = 0; i < MAX_FILES; i++) {
        fs.free_inodes[i] = 0;    // 0 = inode libre
        open_files[i].inode_idx = -1;  // Aucun fichier ouvert
        open_files[i].position = 0;
    }
    fs.free_inode_count = MAX_FILES - 1;  // On réserve l'inode 0
    
    // 3. Configuration du répertoire racine (inode 0)
    strncpy(fs.inodes[0].filename, "/", MAX_FILENAME);
    fs.inodes[0].is_directory = 1;
    fs.inodes[0].is_symlink = 0;
    fs.inodes[0].first_block = -1;  // Pas de bloc alloué
    fs.inodes[0].size = 0;
    fs.inodes[0].parent_dir = 0;    // Auto-référence
    fs.inodes[0].permissions = 0755; // drwxr-xr-x
    fs.inodes[0].creation_time = time(NULL);
    fs.inodes[0].modification_time = time(NULL);
    fs.inodes[0].link_count = 2;    // Standard pour répertoire ('.' et '..')
    fs.free_inodes[0] = 1;          // Marqué comme utilisé
    
    // 4. Initialisation des variables globales
    fs.current_dir = 0;             // Répertoire courant = racine
    strncpy(fs.current_path, "/", MAX_PATH);
    
    // 5. Initialisation des backups
    memset(backups, 0, sizeof(backups));
    memset(backup_times, 0, sizeof(backup_times));
    backup_count = 0;
    
    // Marquer comme initialisé
    initialized = 1;
    
    // Vérification de cohérence
    verify_fs();
    
    // Message de debug
    if (DEBUG) {
        printf("[INIT] Système initialisé. Blocs libres: %d, Inodes libres: %d\n",
              fs.free_block_count, fs.free_inode_count);
    }
}

// Trouve et retourne l’index d’un inode libre.
int find_free_inode() {
    for (int i = 1; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] == 0) {
            fs.free_inodes[i] = 1;
            fs.free_inode_count--;
            return i;
        }
    }
    return -1;
}
/**
 * Déplace un bloc utilisé vers un nouvel emplacement libre
 * param old_block Bloc source (doit être utilisé)
 * param new_block Bloc destination (doit être libre)
 */
// Déplace un bloc de données à un nouvel emplacement.
 void relocate_block(int old_block, int new_block) {
    // Vérifications de sécurité
    if (old_block == new_block || old_block < 0 || new_block < 0 ||
        old_block >= MAX_BLOCKS || new_block >= MAX_BLOCKS ||
        fs.free_blocks[old_block] == 0 || fs.free_blocks[new_block] == 1) {
        return;
    }

    if (DEBUG) {
        printf("[RELOC] Moving block %d to %d\n", old_block, new_block);
    }

    // 1. Copie des données du bloc
    memcpy(&fs.blocks[new_block], &fs.blocks[old_block], sizeof(Block));

    // 2. Mise à jour des références dans les inodes
    for (int i = 0; i < MAX_FILES; i++) {
        if (!fs.free_inodes[i]) continue;

        // Mise à jour du premier bloc
        if (fs.inodes[i].first_block == old_block) {
            fs.inodes[i].first_block = new_block;
        }

        // Mise à jour des liens dans la chaîne
        int current_block = fs.inodes[i].first_block;
        while (current_block != -1) {
            if (fs.blocks[current_block].next == old_block) {
                fs.blocks[current_block].next = new_block;
                break;
            }
            current_block = fs.blocks[current_block].next;
        }
    }

    // 3. Mise à jour des états
    fs.free_blocks[old_block] = 0;
    fs.free_blocks[new_block] = 1;

    if (DEBUG) {
        verify_fs(); // Vérification supplémentaire
    }
}

/**
 * Trouve un bloc libre en priorisant les trous existants
 */
 int find_free_block() {
    verify_fs();
    
    if (fs.free_block_count <= 0) {
        errno = ENOSPC;
        return -1;
    }

    // 1. Chercher d'abord les trous entre blocs utilisés
    for (int i = 1; i < MAX_BLOCKS - 1; i++) {
        if (fs.free_blocks[i] == 0 && 
           (fs.free_blocks[i-1] == 1 || fs.free_blocks[i+1] == 1)) {
            fs.free_blocks[i] = 1;
            fs.free_block_count--;
            return i;
        }
    }

    // 2. Fallback: premier bloc libre standard
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (fs.free_blocks[i] == 0) {
            fs.free_blocks[i] = 1;
            fs.free_block_count--;
            return i;
        }
    }

    errno = ENOSPC;
    return -1;
}

/**
 * Compacte les blocs après une libération
 * param freed_block Bloc qui vient d'être libéré
 */
// Réorganise les blocs pour réduire les espaces vides.
void compact_blocks(int freed_block) {
    // Vérifications de base
    if (freed_block < 0 || freed_block >= MAX_BLOCKS || fs.free_blocks[freed_block] == 1) {
        return;
    }

    // Trouver le dernier bloc utilisé
    int last_used = -1;
    for (int i = MAX_BLOCKS - 1; i >= 0; i--) {
        if (fs.free_blocks[i] == 1) {
            last_used = i;
            break;
        }
    }

    // Si le bloc libéré est avant le dernier utilisé, on compacte
    if (last_used != -1 && freed_block < last_used) {
        relocate_block(last_used, freed_block);
        
        if (DEBUG) {
            printf("[COMPACT] Compacted block %d to %d\n", last_used, freed_block);
        }
    }
}


// Améliorer la défragmentation légère pour être plus efficace
/**
 * Défragmentation légère - réorganise les blocs pour réduire la fragmentation
 */
// Lance une défragmentation partielle (moins agressive).
void defragment_light(void) {
    if (DEBUG) printf("\n[DEFRAG] Début de la défragmentation légère\n");
    int swap_count = 0;
    int last_used = -1;
    time_t start_time = time(NULL);

    // Trouver le dernier bloc utilisé
    for (int i = MAX_BLOCKS - 1; i >= 0; i--) {
        if (fs.free_blocks[i] == 1) {
            last_used = i;
            break;
        }
    }

    if (last_used == -1) {
        if (DEBUG) printf("[DEFRAG] Aucun bloc utilisé - rien à faire\n");
        return;
    }

    // Parcourir jusqu'au dernier bloc utilisé
    for (int i = 0; i < last_used; i++) {
        if (fs.free_blocks[i] == 0) {
            // Trouver le prochain bloc utilisé après i
            for (int j = i + 1; j <= last_used; j++) {
                if (fs.free_blocks[j] == 1) {
                    // Échanger i (libre) et j (utilisé)
                    Block temp = fs.blocks[i];
                    fs.blocks[i] = fs.blocks[j];
                    fs.blocks[j] = temp;

                    // Mettre à jour tous les pointeurs qui référençaient j
                    for (int k = 0; k < MAX_FILES; k++) {
                        if (fs.free_inodes[k]) {
                            // Mise à jour du premier bloc
                            if (fs.inodes[k].first_block == j) {
                                fs.inodes[k].first_block = i;
                            }
                            // Mise à jour des liens dans la chaîne
                            Block *b = &fs.blocks[fs.inodes[k].first_block];
                            while (b->next != -1) {
                                if (b->next == j) {
                                    b->next = i;
                                    break;
                                }
                                b = &fs.blocks[b->next];
                            }
                        }
                    }

                    fs.free_blocks[i] = 1;
                    fs.free_blocks[j] = 0;
                    swap_count++;
                    if (DEBUG) printf("[DEFRAG] Échange blocs %d ↔ %d\n", i, j);
                    break;
                }
            }
        }
    }

    time_t end_time = time(NULL);
    if (DEBUG) {
        printf("[DEFRAG] %d blocs réorganisés en %ld secondes\n", 
              swap_count, end_time - start_time);
    }
    verify_fs();
}


// Résout et retourne la cible d’un lien symbolique.
char *resolve_symlink(int inode_idx) {
    if (inode_idx < 0 || inode_idx >= MAX_FILES || !fs.free_inodes[inode_idx]) {
        fprintf(stderr, "Erreur: Inode de lien invalide %d\n", inode_idx);
        errno = EINVAL;
        return NULL;
    }

    if (!fs.inodes[inode_idx].is_symlink) {
        fprintf(stderr, "Erreur: Inode %d n'est pas un lien symbolique\n", inode_idx);
        errno = EINVAL;
        return NULL;
    }

    int block_idx = fs.inodes[inode_idx].first_block;
    if (block_idx == -1 || block_idx >= MAX_BLOCKS || fs.free_blocks[block_idx] == 0) {
        fprintf(stderr, "Erreur: Bloc de lien invalide %d\n", block_idx);
        errno = EIO;
        return NULL;
    }

    // Vérification que le bloc contient bien une chaîne terminée par NULL
    if (strnlen(fs.blocks[block_idx].data, BLOCK_SIZE) == BLOCK_SIZE) {
        fprintf(stderr, "Erreur: Cible du lien trop longue ou non terminée\n");
        errno = EIO;
        return NULL;
    }

    char *target = strdup(fs.blocks[block_idx].data);
    if (target == NULL) {
        fprintf(stderr, "Erreur: Impossible d'allouer la mémoire pour la cible\n");
        errno = ENOMEM;
        return NULL;
    }

    printf("[DEBUG] Résolution lien %d -> '%s'\n", inode_idx, target);
    return target;
}

// Cherche un fichier ou dossier par nom dans un répertoire donné.
int find_inode_by_name(const char *name, int dir_inode) {
    static int visited_symlinks[MAX_SYMLINK_DEPTH];
    static int current_depth = 0;

    if (name == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Recherche initiale
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && fs.inodes[i].parent_dir == dir_inode && 
            strcmp(fs.inodes[i].filename, name) == 0) {
            
            // Gestion des liens symboliques
            if (fs.inodes[i].is_symlink) {
                if (current_depth >= MAX_SYMLINK_DEPTH) {
                    fprintf(stderr, "Erreur: Profondeur de liens symboliques dépassée\n");
                    errno = ELOOP;
                    return -1;
                }

                // Vérification des boucles
                for (int j = 0; j < current_depth; j++) {
                    if (visited_symlinks[j] == i) {
                        fprintf(stderr, "Erreur: Boucle de liens symboliques détectée\n");
                        errno = ELOOP;
                        return -1;
                    }
                }

                char *target = resolve_symlink(i);
                if (target == NULL) {
                    fprintf(stderr, "Erreur: Lien symbolique corrompu\n");
                    return -1;
                }

                visited_symlinks[current_depth++] = i;
                int resolved_inode = find_inode_by_name(target, dir_inode);
                current_depth--;
                free(target);

                if (resolved_inode == -1) {
                    fprintf(stderr, "Avertissement: Lien symbolique '%s' pointe vers une cible inexistante\n", name);
                    errno = ENOENT;
                }
                return resolved_inode;
            }

            return i;
        }
    }
    
    errno = ENOENT;
    return -1;
}
/**
 * Sauvegarde le système de fichiers après création d'un backup
 * param filename Nom du fichier de sauvegarde
 */
// Sauvegarde l’état actuel du système de fichiers dans un fichier.
 void save_fs(const char *filename) {
    // Création d'un backup avant l'opération
    create_backup();

    if (filename == NULL || strlen(filename) == 0) {
        fprintf(stderr, "Erreur: Nom de fichier invalide\n");
        return;
    }

    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Erreur lors de l'ouverture du fichier");
        return;
    }

    if (fwrite(&fs, sizeof(fs), 1, file) != 1) {
        perror("Erreur lors de l'écriture");
        fclose(file);
        return;
    }

    if (fclose(file) != 0) {
        perror("Erreur lors de la fermeture du fichier");
    }

    printf("Système sauvegardé dans '%s' (%d backups internes disponibles)\n", 
          filename, backup_count);
    verify_fs();
}

// Charge un système de fichiers sauvegardé depuis un fichier.
void load_fs(const char *filename) {
    // Sauvegarde de l'état actuel avant chargement
    if (backup_count > 0) {
        create_backup();
    }

    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Erreur lors du chargement");
        init_fs();
        return;
    }

    if (fread(&fs, sizeof(fs), 1, file) != 1) {
        fprintf(stderr, "Erreur: Fichier corrompu\n");
        fclose(file);
        init_fs();
        return;
    }

    fclose(file);
    
    // Réinitialisation des fichiers ouverts
    for (int i = 0; i < MAX_FILES; i++) {
        open_files[i].inode_idx = -1;
        open_files[i].position = 0;
    }

    printf("Système chargé depuis '%s'\n", filename);
    verify_fs();
    
    // Vérification de la fragmentation
    auto_defragment_if_needed();
}

/**
 * Formate complètement le système de fichiers
 * Réinitialise aussi tous les backups
 */

/**
 * Formate complètement le système de fichiers et réinitialise les backups
 * Affiche des avertissements avant l'opération destructive
 */
// Réinitialise le système de fichiers (formattage complet).
void format_fs() {
    printf("\n\033[1;33mATTENTION: Cette opération va:\033[0m\n");
    printf("- Réinitialiser TOUTES les données du système de fichiers\n");
    printf("- Créer un nouveau système de fichiers vierge\n\n");
    printf("\033[1;31mLes sauvegardes existantes ne seront PAS affectées.\033[0m\n\n");

    // Réinitialisation complète du système de fichiers
    memset(&fs, 0, sizeof(fs));
    
    // NE PAS réinitialiser les backups ici
    // memset(backups, 0, sizeof(backups));
    // memset(backup_times, 0, sizeof(backup_times));
    // backup_count = 0;
    
    // Initialisation des blocs
    for (int i = 0; i < MAX_BLOCKS; i++) {
        fs.free_blocks[i] = 0;
    }
    fs.free_block_count = MAX_BLOCKS;
    
    // Initialisation des inodes
    for (int i = 0; i < MAX_FILES; i++) {
        fs.free_inodes[i] = 0;
    }
    fs.free_inode_count = MAX_FILES - 1;
    
    // Configuration du répertoire racine
    strcpy(fs.inodes[0].filename, "/");
    fs.inodes[0].is_directory = 1;
    fs.inodes[0].first_block = -1;
    fs.inodes[0].size = 0;
    fs.inodes[0].parent_dir = 0;
    fs.inodes[0].permissions = 0755;
    fs.inodes[0].creation_time = time(NULL);
    fs.inodes[0].modification_time = time(NULL);
    fs.free_inodes[0] = 1;
    
    fs.current_dir = 0;
    strcpy(fs.current_path, "/");
    
    printf("\033[1;32mFormatage réussi. Système réinitialisé.\033[0m\n");
    printf("%d blocs et %d inodes libres.\n", 
          fs.free_block_count, fs.free_inode_count);
    printf("\033[1;33mLes %d sauvegardes existantes ont été préservées.\033[0m\n", backup_count);
}


// Ouvre un fichier (lecture, écriture, etc.) et retourne un descripteur de fichier.
int fs_open(const char *filename, int flags) {
    // Recherche de l'inode correspondant au fichier dans le répertoire courant
    int inode_idx = find_inode_by_name(filename, fs.current_dir);
    
    // Si le fichier n'existe pas et que le flag O_CREAT est activé (création d'un fichier si absent)
    if (inode_idx == -1 && (flags & O_CREAT)) {
        // Recherche d'un inode libre pour créer un nouveau fichier
        inode_idx = find_free_inode();
        if (inode_idx == -1) {  // Si aucun inode libre n'est trouvé
            errno = ENOSPC;  // Erreur : espace disque insuffisant
            return -1;  // Retourne -1 en cas d'erreur
        }
        
        // Initialisation de l'inode pour un nouveau fichier
        strcpy(fs.inodes[inode_idx].filename, filename);  // Affecte le nom du fichier à l'inode
        fs.inodes[inode_idx].is_directory = 0;  // Ce n'est pas un répertoire
        fs.inodes[inode_idx].is_symlink = 0;  // Ce n'est pas un lien symbolique
        fs.inodes[inode_idx].first_block = -1;  // Aucun bloc de données associé pour le moment
        fs.inodes[inode_idx].size = 0;  // Taille initiale du fichier (vide pour l'instant)
        fs.inodes[inode_idx].parent_dir = fs.current_dir;  // Répertoire parent du fichier
        fs.inodes[inode_idx].permissions = 0644;  // Permissions par défaut (rw-r--r--)
        fs.inodes[inode_idx].creation_time = time(NULL);  // Temps de création du fichier
        fs.inodes[inode_idx].modification_time = time(NULL);  // Temps de dernière modification du fichier
    } 
    // Si le fichier n'existe pas et que O_CREAT n'est pas spécifié
    else if (inode_idx == -1) {
        errno = ENOENT;  // Erreur : fichier introuvable
        return -1;  // Retourne -1 en cas d'erreur
    }
    
    // Si le fichier trouvé est un répertoire, retourne une erreur
    if (fs.inodes[inode_idx].is_directory) {
        errno = EISDIR;  // Erreur : c'est un répertoire, pas un fichier
        return -1;  // Retourne -1 en cas d'erreur
    }
    
    // Cherche un descripteur de fichier libre pour ouvrir le fichier
    for (int fd = 0; fd < MAX_FILES; fd++) {
        // Si l'emplacement du fichier ouvert est libre
        if (open_files[fd].inode_idx == -1) {
            // Associe l'inode du fichier ouvert au descripteur
            open_files[fd].inode_idx = inode_idx;
            open_files[fd].position = 0;  // Position initiale du fichier (début du fichier)
            return fd;  // Retourne le descripteur de fichier (fd)
        }
    }
    
    // Si aucun descripteur de fichier libre n'est trouvé, retourne une erreur
    errno = EMFILE;  // Erreur : trop de fichiers ouverts
    return -1;  // Retourne -1 en cas d'erreur
}

// Ferme un fichier ouvert identifié par son descripteur.
int fs_close(int fd) {
    if (fd < 0 || fd >= MAX_FILES || open_files[fd].inode_idx == -1) {
        errno = EBADF;
        return -1;
    }
    
    open_files[fd].inode_idx = -1;
    open_files[fd].position = 0;
    return 0;
}

// Lit des données depuis un fichier ouvert dans un tampon.
ssize_t fs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= MAX_FILES || open_files[fd].inode_idx == -1) {
        errno = EBADF;
        return -1;
    }
    
    int inode_idx = open_files[fd].inode_idx;
    off_t position = open_files[fd].position;
    
    if (fs.inodes[inode_idx].is_directory || fs.inodes[inode_idx].is_symlink) {
        errno = fs.inodes[inode_idx].is_directory ? EISDIR : EINVAL;
        return -1;
    }
    
    if (position >= fs.inodes[inode_idx].size) {
        return 0;
    }
    
    size_t bytes_to_read = count;
    if ((off_t)(position + bytes_to_read) > fs.inodes[inode_idx].size) {
        bytes_to_read = fs.inodes[inode_idx].size - position;
    }
    
    ssize_t bytes_read = 0;
    int block_idx = fs.inodes[inode_idx].first_block;
    char *buffer = (char *)buf;
    
    int blocks_to_skip = position / BLOCK_SIZE;
    while (block_idx != -1 && blocks_to_skip > 0) {
        block_idx = fs.blocks[block_idx].next;
        blocks_to_skip--;
    }
    
    if (block_idx == -1) {
        return 0;
    }
    
    int offset_in_block = position % BLOCK_SIZE;
    while (block_idx != -1 && bytes_read < (ssize_t)bytes_to_read) {
        int bytes_in_block = BLOCK_SIZE - offset_in_block;
        if (bytes_in_block > (int)(bytes_to_read - bytes_read)) {
            bytes_in_block = bytes_to_read - bytes_read;
        }
        
        memcpy(buffer + bytes_read, fs.blocks[block_idx].data + offset_in_block, bytes_in_block);
        bytes_read += bytes_in_block;
        offset_in_block = 0;
        block_idx = fs.blocks[block_idx].next;
    }
    
    open_files[fd].position += bytes_read;
    return bytes_read;
}

// Écrit des données dans un fichier ouvert depuis un tampon.
ssize_t fs_write(int fd, const void *buf, size_t count) {
    // Vérification du file descriptor
    if (fd < 0 || fd >= MAX_FILES || open_files[fd].inode_idx == -1) {
        fprintf(stderr, "Erreur: File descriptor %d invalide\n", fd);
        errno = EBADF;
        return -1;
    }
    
    int inode_idx = open_files[fd].inode_idx;
    off_t position = open_files[fd].position;

    // Vérification du type de fichier
    if (fs.inodes[inode_idx].is_directory) {
        fprintf(stderr, "Erreur: %s est un répertoire\n", fs.inodes[inode_idx].filename);
        errno = EISDIR;
        return -1;
    }
    
    if (fs.inodes[inode_idx].is_symlink) {
        printf("Avertissement: Écriture dans le lien symbolique lui-même\n");
    }

    // Vérification des permissions
    if ((fs.inodes[inode_idx].permissions & 0200) == 0) {
        fprintf(stderr, "Erreur: Permission refusée pour %s\n", fs.inodes[inode_idx].filename);
        errno = EACCES;
        return -1;
    }

    printf("Début écriture dans %s (position: %ld, taille: %zu octets)\n",
          fs.inodes[inode_idx].filename, position, count);

    // Allocation du premier bloc si nécessaire
    if (fs.inodes[inode_idx].first_block == -1) {
        printf("Allocation du premier bloc...\n");
        int new_block = find_free_block();
        if (new_block == -1) {
            fprintf(stderr, "Échec allocation bloc initial\n");
            return -1;
        }
        fs.inodes[inode_idx].first_block = new_block;
        fs.blocks[new_block].next = -1;
        memset(fs.blocks[new_block].data, 0, BLOCK_SIZE);
    }

    const char *buffer = (const char *)buf;
    ssize_t bytes_written = 0;
    int current_block = fs.inodes[inode_idx].first_block;
    int original_block = current_block;

    // Navigation jusqu'au bloc de départ
    int blocks_to_skip = position / BLOCK_SIZE;
    while (blocks_to_skip > 0 && current_block != -1) {
        if (fs.blocks[current_block].next == -1) {
            int new_block = find_free_block();
            if (new_block == -1) {
                fprintf(stderr, "Espace insuffisant pour atteindre la position %ld\n", position);
                break;
            }
            fs.blocks[current_block].next = new_block;
            fs.blocks[new_block].next = -1;
            memset(fs.blocks[new_block].data, 0, BLOCK_SIZE);
        }
        current_block = fs.blocks[current_block].next;
        blocks_to_skip--;
    }

    if (current_block == -1) {
        fprintf(stderr, "Position %ld invalide\n", position);
        errno = ENOSPC;
        return -1;
    }

    // Écriture effective
    int offset_in_block = position % BLOCK_SIZE;
    while (bytes_written < (ssize_t)count && current_block != -1) {
        int space_in_block = BLOCK_SIZE - offset_in_block;
        int bytes_to_write = ((size_t)(count - bytes_written) < (size_t)space_in_block) ? 
                           (int)(count - bytes_written) : space_in_block;
        
        memcpy(fs.blocks[current_block].data + offset_in_block, 
              buffer + bytes_written, bytes_to_write);
        
        bytes_written += bytes_to_write;
        offset_in_block = 0;

        if (bytes_written < (ssize_t)count) {
            if (fs.blocks[current_block].next == -1) {
                int new_block = find_free_block();
                if (new_block == -1) {
                    fprintf(stderr, "Espace insuffisant pour compléter l'écriture\n");
                    break;
                }
                fs.blocks[current_block].next = new_block;
                fs.blocks[new_block].next = -1;
                memset(fs.blocks[new_block].data, 0, BLOCK_SIZE);
            }
            current_block = fs.blocks[current_block].next;
        }
    }

    
        // Après une écriture réussie
        if (bytes_written > 0) {
            time_t now = time(NULL);
            off_t new_size = position + bytes_written;
            
            if (new_size > fs.inodes[inode_idx].size) {
                fs.inodes[inode_idx].size = new_size;
                
                // Mise à jour de tous les hard links
                if (!fs.inodes[inode_idx].is_symlink) {
                    for (int i = 0; i < MAX_INODES; i++) {
                        if (i != inode_idx && fs.free_inodes[i] && 
                            fs.inodes[i].first_block == original_block && 
                            !fs.inodes[i].is_directory) {
                            fs.inodes[i].size = new_size;
                            fs.inodes[i].modification_time = now;
                            printf("Mise à jour hard link %s (inode %d)\n",
                                  fs.inodes[i].filename, i);
                        }
                    }
                }
            }
            fs.inodes[inode_idx].modification_time = now;
        }
    
    

    open_files[fd].position += bytes_written;
    printf("Écriture réussie: %zd octets écrits dans %s\n", 
          bytes_written, fs.inodes[inode_idx].filename);
    
    verify_fs();
    return bytes_written;
}

// Modifie la position de lecture/écriture dans un fichier ouvert, selon SEEK_SET, SEEK_CUR ou SEEK_END.
off_t fs_lseek(int fd, off_t offset, int whence) {
    if (fd < 0 || fd >= MAX_FILES || open_files[fd].inode_idx == -1) {
        errno = EBADF;
        return -1;
    }
    
    int inode_idx = open_files[fd].inode_idx;
    off_t new_pos;
    
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = open_files[fd].position + offset;
            break;
        case SEEK_END:
            new_pos = fs.inodes[inode_idx].size + offset;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    
    if (new_pos < 0) {
        errno = EINVAL;
        return -1;
    }
    
    open_files[fd].position = new_pos;
    return new_pos;
}

// Crée un répertoire (optionnellement de manière récursive).
int fs_mkdir(const char *path, mode_t mode, int recursive) {
    if (path == NULL || strlen(path) == 0) {
        errno = EINVAL;
        return -1;
    }

    char temp_path[MAX_PATH];
    strncpy(temp_path, path, MAX_PATH-1);
    temp_path[MAX_PATH-1] = '\0';

    int current = fs.current_dir;
    char *token = strtok(temp_path, "/");
    char *last_token = NULL;

    while (token != NULL) {
        last_token = token;
        token = strtok(NULL, "/");

        if (token != NULL) { // Ce n'est pas encore le dernier token
            int next = find_inode_by_name(last_token, current);
            
            if (next == -1) {
                if (!recursive) {
                    errno = ENOENT;
                    return -1;
                }
                
                // Création du répertoire intermédiaire
                next = find_free_inode();
                if (next == -1) {
                    errno = ENOSPC;
                    return -1;
                }
                
                // Initialisation du nouvel inode
                strncpy(fs.inodes[next].filename, last_token, MAX_FILENAME-1);
                fs.inodes[next].filename[MAX_FILENAME-1] = '\0';
                fs.inodes[next].is_directory = 1;
                fs.inodes[next].parent_dir = current;
                fs.inodes[next].permissions = 0755; // Permissions par défaut
                fs.inodes[next].creation_time = time(NULL);
                fs.inodes[next].modification_time = time(NULL);
                
                // Ajout au répertoire parent
                if (add_entry_to_dir(current, next) == -1) {
                    return -1;
                }
            }
            
            if (!fs.inodes[next].is_directory) {
                errno = ENOTDIR;
                return -1;
            }
            
            current = next;
        }
    }

    // Vérification si le répertoire final existe déjà
    if (find_inode_by_name(last_token, current) != -1) {
        errno = EEXIST;
        return -1;
    }

    // Création du répertoire final
    int dir_inode = find_free_inode();
    if (dir_inode == -1) {
        errno = ENOSPC;
        return -1;
    }

    strncpy(fs.inodes[dir_inode].filename, last_token, MAX_FILENAME-1);
    fs.inodes[dir_inode].filename[MAX_FILENAME-1] = '\0';
    fs.inodes[dir_inode].is_directory = 1;
    fs.inodes[dir_inode].parent_dir = current;
    fs.inodes[dir_inode].permissions = mode;
    fs.inodes[dir_inode].creation_time = time(NULL);
    fs.inodes[dir_inode].modification_time = time(NULL);

    return add_entry_to_dir(current, dir_inode);
}

// Ajoute une entrée dans un répertoire pour un fichier (inode).
int add_entry_to_dir(int dir_inode, int new_inode) {
    // Vérifie si le répertoire a de la place
    if (fs.inodes[dir_inode].num_entries >= MAX_DIR_ENTRIES) {
        errno = ENOSPC;
        return -1;
    }
    
    // Ajoute la nouvelle entrée
    fs.inodes[dir_inode].entries[fs.inodes[dir_inode].num_entries++] = new_inode;
    fs.inodes[dir_inode].modification_time = time(NULL);
    
    return 0;
}

// Supprime un répertoire (vide).
int fs_rmdir(const char *path) {
    if (path == NULL || strlen(path) == 0) {
        errno = EINVAL;
        return -1;
    }

    char path_copy[MAX_PATH];
    strncpy(path_copy, path, MAX_PATH-1);
    path_copy[MAX_PATH-1] = '\0';

    int current_dir = fs.current_dir;
    char *token = strtok(path_copy, "/");
    char *last_token = NULL;

    // Parcours du chemin
    while (token != NULL) {
        last_token = token;
        token = strtok(NULL, "/");

        if (token != NULL) { // Ce n'est pas encore le dernier token
            int next = find_inode_by_name(last_token, current_dir);
            if (next == -1) {
                errno = ENOENT;
                return -1;
            }
            if (!fs.inodes[next].is_directory) {
                errno = ENOTDIR;
                return -1;
            }
            current_dir = next;
        }
    }

    // Vérification sur le répertoire final
    int dir_inode = find_inode_by_name(last_token, current_dir);
    if (dir_inode == -1) {
        errno = ENOENT;
        return -1;
    }

    if (!fs.inodes[dir_inode].is_directory) {
        errno = ENOTDIR;
        return -1;
    }

    // Vérification que le répertoire est vide
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && fs.inodes[i].parent_dir == dir_inode) {
            errno = ENOTEMPTY;
            return -1;
        }
    }

    // Suppression
    fs.free_inodes[dir_inode] = 0;
    fs.free_inode_count++;

    return 0;
}

// Retourne le chemin du répertoire de travail courant.
char *fs_getcwd(char *buf, size_t size) {
    if (buf == NULL) {
        buf = malloc(MAX_PATH);
        if (buf == NULL) {
            errno = ENOMEM;
            return NULL;
        }
    }
    
    if (size < strlen(fs.current_path) + 1) {
        errno = ERANGE;
        return NULL;
    }
    
    strcpy(buf, fs.current_path);
    return buf;
}

/**
 * Supprime un fichier en créant un backup automatique avant l'opération
 * @aram path Chemin du fichier à supprimer
 * @eturn 0 si succès, -1 si erreur (errno est défini)
 */
// Supprime un fichier (unlink).
 int fs_unlink(const char *path) {

    
    if (path == NULL || strlen(path) == 0) {
        fprintf(stderr, "Erreur: Chemin invalide\n");
        errno = EINVAL;
        return -1;
    }

    int inode_idx = find_inode_by_name(path, fs.current_dir);
    if (inode_idx == -1) {
        fprintf(stderr, "Erreur: Fichier '%s' introuvable\n", path);
        errno = ENOENT;
        return -1;
    }

    if (fs.inodes[inode_idx].is_directory) {
        fprintf(stderr, "Erreur: '%s' est un répertoire\n", path);
        errno = EISDIR;
        return -1;
    }

    // Suppression des blocs de données
    int block_idx = fs.inodes[inode_idx].first_block;
    while (block_idx != -1) {
        int next_block = fs.blocks[block_idx].next;
        fs.free_blocks[block_idx] = 0;
        fs.free_block_count++;
        compact_blocks(block_idx);
        block_idx = next_block;
    }

    // Libération de l'inode
    fs.free_inodes[inode_idx] = 0;
    fs.free_inode_count++;

    if (DEBUG) {
        printf("Fichier '%s' supprimé \n", path);
    }
    
    verify_fs();
    return 0;
}


/**
 * Crée un point de restauration du système de fichiers actuel
 */
// Crée une sauvegarde complète du système de fichiers en mémoire.
void create_backup() {
    // Vérifier si on a atteint le nombre maximum de sauvegardes
    if (backup_count >= MAX_BACKUPS) {
        // Décaler les backups pour faire de la place (FIFO)
        for (int i = 0; i < MAX_BACKUPS - 1; i++) {
            memcpy(&backups[i], &backups[i + 1], sizeof(FileSystem));
            backup_times[i] = backup_times[i + 1];
        }
        backup_count--;
    }

    // Créer le nouveau backup
    memcpy(&backups[backup_count], &fs, sizeof(FileSystem));
    backup_times[backup_count] = time(NULL);
    backup_count++;

    if (DEBUG) {
        char time_str[50];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&backup_times[backup_count-1]));
        printf("[BACKUP] Créé à %s (total: %d)\n", time_str, backup_count);
    }
}

//Supprimer backup
void remove_single_backup(int backup_num) {
    if (backup_num < 0 || backup_num >= backup_count) {
        printf("\033[1;31mErreur: Numéro de backup invalide (doit être entre 0 et %d)\033[0m\n", backup_count-1);
        return;
    }

    printf("\033[1;33mSuppression du backup [%d]...\033[0m\n", backup_num);
    
    // Décaler les backups suivants
    for (int i = backup_num; i < backup_count - 1; i++) {
        memcpy(&backups[i], &backups[i+1], sizeof(FileSystem));
        backup_times[i] = backup_times[i+1];
    }
    
    // Réinitialiser le dernier élément
    memset(&backups[backup_count-1], 0, sizeof(FileSystem));
    backup_times[backup_count-1] = 0;
    
    backup_count--;
    printf("\033[1;32m✓ Backup %d supprimé avec succès\033[0m\n", backup_num);
    printf("Il reste %d backup(s) disponible(s)\n", backup_count);
}


/**
 * Affiche la liste des sauvegardes disponibles avec leurs timestamps
 */
// Liste toutes les sauvegardes disponibles avec leurs dates.
void list_backups() {
    printf("\n=== BACKUPS DISPONIBLES (%d/%d) ===\n", backup_count, MAX_BACKUPS);
    if (backup_count == 0) {
        printf("Aucun backup disponible\n");
        printf("===============================\n");
        return;
    }

    for (int i = 0; i < backup_count; i++) {
        char time_str[50];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&backup_times[i]));
        printf("[%d] %s\n", i, time_str);
    }
    printf("===============================\n");
}

/**
 * Restaure le système de fichiers à partir d'un backup spécifique
 * param index Index du backup à restaurer
 * return 0 si succès, -1 si échec
 */
// Restaure une sauvegarde à partir de son index.
int restore_backup(int index) {
    if (index < 0 || index >= backup_count) {
        fprintf(stderr, "Erreur: Index de backup invalide (doit être entre 0 et %d)\n", backup_count-1);
        errno = EINVAL;
        return -1;
    }

    // Fermer tous les fichiers ouverts
    for (int i = 0; i < MAX_FILES; i++) {
        if (open_files[i].inode_idx != -1) {
            if (DEBUG) {
                printf("[RESTORE] Fermeture FD %d (inode %d)\n", 
                      i, open_files[i].inode_idx);
            }
            open_files[i].inode_idx = -1;
            open_files[i].position = 0;
        }
    }

    // Restaurer le système de fichiers
    memcpy(&fs, &backups[index], sizeof(FileSystem));
    
    // Mettre à jour les métadonnées importantes
    time_t now = time(NULL);
    fs.inodes[0].modification_time = now;  // Mettre à jour le répertoire racine
    
    char time_str[50];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&backup_times[index]));
    printf("Système restauré à partir du backup du %s\n", time_str);
    
    // Vérifier l'intégrité du système restauré
    verify_fs();
    return 0;
}

void normalize_path(char *path) {
    if (path == NULL || strlen(path) == 0) return;

    char *p = path;
    char *q = path;
    
    // Gestion du début du chemin
    if (*p == '/') {
        *q++ = '/';
        p++;
        while (*p == '/') p++; // Ignorer les '/' supplémentaires au début
    }
    
    while (*p) {
        *q++ = *p++;
        if (*(p-1) == '/') {
            while (*p == '/') p++; // Ignorer les '/' consécutifs
        }
    }
    
    // Supprimer le dernier '/' sauf pour la racine
    if (q > path + 1 && *(q-1) == '/') {
        q--;
    }
    
    *q = '\0';
    
    // Gestion spéciale pour le chemin racine
    if (strcmp(path, "") == 0) {
        strcpy(path, "/");
    }
}



//pGestion des chemins
// Change le répertoire de travail courant.
int fs_chdir(const char *path) {
    if (path == NULL || strlen(path) == 0) {
        errno = EINVAL;
        return -1;
    }

    char path_copy[MAX_PATH];
    strncpy(path_copy, path, MAX_PATH - 1);
    path_copy[MAX_PATH - 1] = '\0';

    // Cas spéciaux
    if (strcmp(path, ".") == 0) {
        return 0; // Ne rien faire
    }
    if (strcmp(path, "..") == 0) {
        return handle_parent_dir();
    }
    if (strcmp(path, "/") == 0) {
        fs.current_dir = 0;
        strcpy(fs.current_path, "/");
        return 0;
    }

    int target_dir = fs.current_dir;
    char new_path[MAX_PATH] = "";

    // Gestion des chemins absolus
    if (path[0] == '/') {
        target_dir = 0;
        strcpy(new_path, "/");
    } else {
        // Pour les chemins relatifs, on part du chemin courant
        strcpy(new_path, fs.current_path);
        if (strcmp(fs.current_path, "/") != 0) {
            strcat(new_path, "/");
        }
    }

    char *token = strtok(path_copy, "/");
    while (token != NULL) {
        if (strcmp(token, ".") == 0) {
            token = strtok(NULL, "/");
            continue;
        }

        if (strcmp(token, "..") == 0) {
            if (target_dir != 0) { // Si pas déjà à la racine
                target_dir = fs.inodes[target_dir].parent_dir;
                
                // Mise à jour du chemin affiché
                char *last_slash = strrchr(new_path, '/');
                if (last_slash != new_path) {
                    *last_slash = '\0';
                } else {
                    new_path[1] = '\0';
                }
            }
            token = strtok(NULL, "/");
            continue;
        }

        int next = find_inode_by_name(token, target_dir);
        if (next == -1) {
            errno = ENOENT;
            return -1;
        }
        if (!fs.inodes[next].is_directory) {
            errno = ENOTDIR;
            return -1;
        }

        target_dir = next;
        
        // Mise à jour du chemin affiché
        if (strcmp(new_path, "/") != 0 && new_path[strlen(new_path)-1] != '/') {
            strcat(new_path, "/");
        }
        strcat(new_path, token);
        
        token = strtok(NULL, "/");
    }

    // Normalisation finale du chemin
    normalize_path(new_path);
    strcpy(fs.current_path, new_path);
    fs.current_dir = target_dir;
    
    return 0;
}

static int handle_parent_dir() {
    if (fs.current_dir == 0) return 0; // Déjà à la racine
    
    // Mettre à jour le répertoire courant
    fs.current_dir = fs.inodes[fs.current_dir].parent_dir;
    
    // Mettre à jour le chemin affiché
    char *last_slash = strrchr(fs.current_path, '/');
    if (last_slash == fs.current_path) {
        // Cas où on est juste en dessous de la racine
        fs.current_path[1] = '\0';
    } else if (last_slash != NULL) {
        *last_slash = '\0';
    }
    
    // Normaliser le chemin
    normalize_path(fs.current_path);
    
    return 0;
}

// Change les permissions d’un fichier ou dossier.
int fs_chmod(const char *path, mode_t mode) {
    if (path == NULL || strlen(path) == 0) {
        errno = EINVAL;
        return -1;
    }
    
    int inode_idx = find_inode_by_name(path, fs.current_dir);
    
    if (inode_idx == -1) {
        errno = ENOENT;
        return -1;
    }
    
    fs.inodes[inode_idx].permissions = mode;
    fs.inodes[inode_idx].modification_time = time(NULL);
    return 0;
}

// Crée un lien physique vers un fichier existant.
int fs_link(const char *oldpath, const char *newpath) {
    if (oldpath == NULL || newpath == NULL || strlen(oldpath) == 0 || strlen(newpath) == 0) {
        fprintf(stderr, "Erreur: Paramètres invalides\n");
        errno = EINVAL;
        return -1;
    }

    int old_inode = find_inode_by_name(oldpath, fs.current_dir);
    if (old_inode == -1) {
        fprintf(stderr, "Erreur: Fichier source '%s' inexistant\n", oldpath);
        errno = ENOENT;
        return -1;
    }

    if (fs.inodes[old_inode].is_directory) {
        fprintf(stderr, "Erreur: Impossible de lier un répertoire\n");
        errno = EPERM;
        return -1;
    }

    if (find_inode_by_name(newpath, fs.current_dir) != -1) {
        fprintf(stderr, "Erreur: '%s' existe déjà\n", newpath);
        errno = EEXIST;
        return -1;
    }

    int new_inode = find_free_inode();
    if (new_inode == -1) {
        fprintf(stderr, "Erreur: Plus d'inodes disponibles\n");
        errno = ENOSPC;
        return -1;
    }

        // Configuration du hard link
        strcpy(fs.inodes[new_inode].filename, newpath);
        fs.inodes[new_inode].is_directory = 0;
        fs.inodes[new_inode].is_symlink = 0;
        fs.inodes[new_inode].first_block = fs.inodes[old_inode].first_block; // Partage le même bloc
        fs.inodes[new_inode].size = fs.inodes[old_inode].size;
        fs.inodes[new_inode].parent_dir = fs.current_dir;
        fs.inodes[new_inode].permissions = fs.inodes[old_inode].permissions;
        fs.inodes[new_inode].creation_time = time(NULL);
        fs.inodes[new_inode].modification_time = fs.inodes[old_inode].modification_time;
        
        // Mise à jour du compteur de liens
        fs.inodes[old_inode].link_count++;
        fs.inodes[new_inode].link_count = fs.inodes[old_inode].link_count;
    
        fs.free_inodes[new_inode] = 1;
        fs.free_inode_count--;
    
        printf("Lien physique créé: %s -> %s (inode: %d, liens: %d, bloc: %d)\n", 
              newpath, oldpath, old_inode, fs.inodes[old_inode].link_count, 
              fs.inodes[old_inode].first_block);
        
        return 0;
    }

    // Met à jour les métadonnées de tous les liens physiques d’un même inode.
    void update_all_hardlinks(int inode_num, size_t new_size, time_t mtime) {
        if (inode_num < 0 || inode_num >= MAX_INODES || fs.inodes[inode_num].is_symlink) 
            return;
        
        int original_block = fs.inodes[inode_num].first_block;
        
        for (int i = 0; i < MAX_INODES; i++) {
            if (i != inode_num && fs.free_inodes[i] && 
                fs.inodes[i].first_block == original_block && 
                !fs.inodes[i].is_directory) {
                fs.inodes[i].size = new_size;
                fs.inodes[i].modification_time = mtime;
            }
        }
    }

// Crée un lien symbolique vers une cible.
int fs_symlink(const char *target, const char *linkpath) {
    if (target == NULL || linkpath == NULL || strlen(target) == 0 || strlen(linkpath) == 0) {
        fprintf(stderr, "Erreur: Paramètres invalides\n");
        errno = EINVAL;
        return -1;
    }

    if (find_inode_by_name(linkpath, fs.current_dir) != -1) {
        fprintf(stderr, "Erreur: '%s' existe déjà\n", linkpath);
        errno = EEXIST;
        return -1;
    }

    int link_inode = find_free_inode();
    if (link_inode == -1) {
        fprintf(stderr, "Erreur: Plus d'inodes disponibles\n");
        errno = ENOSPC;
        return -1;
    }

    int link_block = find_free_block();
    if (link_block == -1) {
        fs.free_inodes[link_inode] = 0; // Annule l'allocation de l'inode
        fprintf(stderr, "Erreur: Plus de blocs disponibles\n");
        errno = ENOSPC;
        return -1;
    }

    // Stockage du chemin cible
    strncpy(fs.blocks[link_block].data, target, BLOCK_SIZE - 1);
    fs.blocks[link_block].data[BLOCK_SIZE - 1] = '\0';
    fs.blocks[link_block].next = -1;

    // Configuration de l'inode du lien
    strcpy(fs.inodes[link_inode].filename, linkpath);
    fs.inodes[link_inode].is_directory = 0;
    fs.inodes[link_inode].is_symlink = 1;
    fs.inodes[link_inode].first_block = link_block;
    fs.inodes[link_inode].size = strlen(target);
    fs.inodes[link_inode].parent_dir = fs.current_dir;
    fs.inodes[link_inode].permissions = 0777;
    fs.inodes[link_inode].creation_time = time(NULL);
    fs.inodes[link_inode].modification_time = time(NULL);
    fs.inodes[link_inode].link_count = 1;

    printf("Lien symbolique créé: %s -> %s\n", linkpath, target);
    
    return 0;
}

// Affiche le contenu d’un répertoire (comme `ls`).
void fs_ls(const char *path) {
    int target_dir = fs.current_dir;
    char display_path[MAX_PATH] = "";
    
    // Traitement des chemins imbriqués
    if (path != NULL && strlen(path) > 0) {
        char path_copy[MAX_PATH];
        strncpy(path_copy, path, MAX_PATH-1);
        
        char *token = strtok(path_copy, "/");
        while (token != NULL) {
            int next = find_inode_by_name(token, target_dir);
            
            if (next == -1) {
                printf("ls: %s: Aucun fichier ou dossier de ce type\n", path);
                return;
            }
            
            if (!fs.inodes[next].is_directory) {
                // Si le dernier élément est un fichier, on l'affiche directement
                if (strtok(NULL, "/") == NULL) {
                    printf("%s\n", fs.inodes[next].filename);
                    return;
                } else {
                    printf("ls: %s: N'est pas un répertoire\n", token);
                    return;
                }
            }
            
            target_dir = next;
            strncat(display_path, token, MAX_PATH-strlen(display_path)-1);
            strncat(display_path, "/", MAX_PATH-strlen(display_path)-1);
            token = strtok(NULL, "/");
        }
    } else {
        strncpy(display_path, fs.current_path, MAX_PATH-1);
    }
    
    // Affichage du contenu
    printf("Contenu de '%s':\n", display_path);
    printf("Type Permissions  Taille Nom\n");
    printf("---- ----------- ------- ----------------\n");
    
    int dir_count = 0, file_count = 0, link_count = 0;
    
    // Afficher d'abord les répertoires
    printf("\033[1;34m[REPERTOIRES]\033[0m\n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && fs.inodes[i].parent_dir == target_dir && 
            fs.inodes[i].is_directory) {
            printf("d    %c%c%c%c%c%c%c%c%c %7d %s/\n",
                (fs.inodes[i].permissions & 0400) ? 'r' : '-',
                (fs.inodes[i].permissions & 0200) ? 'w' : '-',
                (fs.inodes[i].permissions & 0100) ? 'x' : '-',
                (fs.inodes[i].permissions & 0040) ? 'r' : '-',
                (fs.inodes[i].permissions & 0020) ? 'w' : '-',
                (fs.inodes[i].permissions & 0010) ? 'x' : '-',
                (fs.inodes[i].permissions & 0004) ? 'r' : '-',
                (fs.inodes[i].permissions & 0002) ? 'w' : '-',
                (fs.inodes[i].permissions & 0001) ? 'x' : '-',
                fs.inodes[i].size,
                fs.inodes[i].filename);
            dir_count++;
        }
    }
    
    // Afficher les fichiers réguliers
    printf("\n\033[1;32m[FICHIERS]\033[0m\n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && fs.inodes[i].parent_dir == target_dir && 
            !fs.inodes[i].is_directory && !fs.inodes[i].is_symlink) {
            printf("-    %c%c%c%c%c%c%c%c%c %7d %s\n",
                (fs.inodes[i].permissions & 0400) ? 'r' : '-',
                (fs.inodes[i].permissions & 0200) ? 'w' : '-',
                (fs.inodes[i].permissions & 0100) ? 'x' : '-',
                (fs.inodes[i].permissions & 0040) ? 'r' : '-',
                (fs.inodes[i].permissions & 0020) ? 'w' : '-',
                (fs.inodes[i].permissions & 0010) ? 'x' : '-',
                (fs.inodes[i].permissions & 0004) ? 'r' : '-',
                (fs.inodes[i].permissions & 0002) ? 'w' : '-',
                (fs.inodes[i].permissions & 0001) ? 'x' : '-',
                fs.inodes[i].size,
                fs.inodes[i].filename);
            file_count++;
        }
    }
    
    // Afficher les liens symboliques
    printf("\n\033[1;36m[LIENS]\033[0m\n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && fs.inodes[i].parent_dir == target_dir && 
            fs.inodes[i].is_symlink) {
            char *target = resolve_symlink(i);
            printf("l    %c%c%c%c%c%c%c%c%c %7d %s -> %s\n",
                (fs.inodes[i].permissions & 0400) ? 'r' : '-',
                (fs.inodes[i].permissions & 0200) ? 'w' : '-',
                (fs.inodes[i].permissions & 0100) ? 'x' : '-',
                (fs.inodes[i].permissions & 0040) ? 'r' : '-',
                (fs.inodes[i].permissions & 0020) ? 'w' : '-',
                (fs.inodes[i].permissions & 0010) ? 'x' : '-',
                (fs.inodes[i].permissions & 0004) ? 'r' : '-',
                (fs.inodes[i].permissions & 0002) ? 'w' : '-',
                (fs.inodes[i].permissions & 0001) ? 'x' : '-',
                fs.inodes[i].size,
                fs.inodes[i].filename,
                target ? target : "?");
            if (target) free(target);
            link_count++;
        }
    }
    
    // Affichage du total
    printf("\nTotal: %d répertoire(s), %d fichier(s), %d lien(s)\n", 
          dir_count, file_count, link_count);
}

// Affiche les détails étendus d’un répertoire (comme `ls -l`).
void fs_ls_l(const char *path) {
    int dir_inode;
    int dir_count = 0, file_count = 0, link_count = 0;

    if (path == NULL || strlen(path) == 0) {
        dir_inode = fs.current_dir;
        printf("Contenu du répertoire courant '%s':\n", fs.current_path);
    } else {
        dir_inode = find_inode_by_name(path, fs.current_dir);
        if (dir_inode == -1) {
            printf("ls: %s: Aucun fichier ou dossier de ce type\n", path);
            return;
        }

        if (!fs.inodes[dir_inode].is_directory) {
            // Afficher les infos détaillées du fichier seul
            printf("Type  Liens  Permissions    Taille  Date                Nom\n");
            printf("----  -----  -----------  -------  -------------------  ----------------\n");
            print_file_details_long(dir_inode);
            return;
        }

        printf("Contenu du répertoire '%s':\n", path);
    }

    printf("Type  Liens  Permissions    Taille  Date                Nom\n");
    printf("----  -----  -----------  -------  -------------------  ----------------\n");

    // Afficher d'abord les répertoires
    printf("\033[1;34m[REPERTOIRES]\033[0m\n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && fs.inodes[i].parent_dir == dir_inode && 
            fs.inodes[i].is_directory) {
            print_file_details_long(i);
            dir_count++;
        }
    }

    // Afficher les fichiers réguliers
    printf("\n\033[1;32m[FICHIERS]\033[0m\n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && fs.inodes[i].parent_dir == dir_inode && 
            !fs.inodes[i].is_directory && !fs.inodes[i].is_symlink) {
            print_file_details_long(i);
            file_count++;
        }
    }

    // Afficher les liens symboliques
    printf("\n\033[1;36m[LIENS]\033[0m\n");
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && fs.inodes[i].parent_dir == dir_inode && 
            fs.inodes[i].is_symlink) {
            print_file_details_long(i);
            char *target = resolve_symlink(i);
            printf(" -> %s\n", target ? target : "?");
            if (target) free(target);
            link_count++;
        }
    }

    printf("\nTotal: %d répertoire(s), %d fichier(s), %d lien(s)\n", 
          dir_count, file_count, link_count);
}

// Affiche les détails d’un fichier de manière détaillée (`ls -l` par inode).
void print_file_details_long(int inode_idx) {
    char type = fs.inodes[inode_idx].is_directory ? 'd' : 
               (fs.inodes[inode_idx].is_symlink ? 'l' : '-');
    
    printf("%c    %3d  ", type, fs.inodes[inode_idx].link_count);

    // Permissions
    printf("%c%c%c%c%c%c%c%c%c ", 
          (fs.inodes[inode_idx].permissions & 0400) ? 'r' : '-',
          (fs.inodes[inode_idx].permissions & 0200) ? 'w' : '-',
          (fs.inodes[inode_idx].permissions & 0100) ? 'x' : '-',
          (fs.inodes[inode_idx].permissions & 0040) ? 'r' : '-',
          (fs.inodes[inode_idx].permissions & 0020) ? 'w' : '-',
          (fs.inodes[inode_idx].permissions & 0010) ? 'x' : '-',
          (fs.inodes[inode_idx].permissions & 0004) ? 'r' : '-',
          (fs.inodes[inode_idx].permissions & 0002) ? 'w' : '-',
          (fs.inodes[inode_idx].permissions & 0001) ? 'x' : '-');

    // Taille
    printf("%7d  ", fs.inodes[inode_idx].size);

    // Date
    char date[20];
    struct tm *tm_info = localtime(&fs.inodes[inode_idx].modification_time);
    strftime(date, sizeof(date), "%b %d %H:%M", tm_info);
    printf("%s  ", date);

    // Nom
    printf("%s", fs.inodes[inode_idx].filename);

    // Ajout du / pour les répertoires
    if (fs.inodes[inode_idx].is_directory) {
        printf("/");
    }
    
    printf("\n");
}


// Affiche le contenu d’un fichier (comme `cat`).
void fs_cat(const char *filename) {
    if (filename == NULL || strlen(filename) == 0) {
        printf("cat: Nom de fichier manquant\n");
        return;
    }
    
    int fd = fs_open(filename, O_RDONLY);
    if (fd == -1) {
        printf("cat: %s: %s\n", filename, strerror(errno));
        return;
    }
    
    char buffer[BLOCK_SIZE];
    ssize_t bytes;
    ssize_t total_bytes = 0;
    
    printf("Contenu du fichier '%s':\n", filename);
    printf("----------------------------------------\n");
    
    while ((bytes = fs_read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes] = '\0';
        printf("%s", buffer);
        total_bytes += bytes;
    }
    
    printf("\n----------------------------------------\n");
    printf("Fin du fichier (total: %zd octets)\n", total_bytes);
    
    fs_close(fd);
}

// Crée un fichier vide ou met à jour sa date de modification (comme `touch`).
int fs_touch(const char *filename) {
    if (filename == NULL || strlen(filename) == 0) {
        printf("touch: Nom de fichier manquant\n");
        return -1;
    }
    
    int exists = find_inode_by_name(filename, fs.current_dir);
    
    int fd = fs_open(filename, O_CREAT | O_WRONLY);
    if (fd == -1) {
        printf("touch: échec de création de '%s': %s\n", filename, strerror(errno));
        return -1;
    }
    
    fs_close(fd);
    
    if (exists == -1) {
        printf("Fichier '%s' créé avec succès\n", filename);
    } else {
        printf("Horodatage du fichier '%s' mis à jour\n", filename);
    }
    
    return 0;
}

// Écrit du texte dans un fichier (comme `echo "..." > file`).
int fs_echo(const char *text, const char *filename) {
    if (text == NULL || filename == NULL || strlen(filename) == 0) {
        printf("echo: Paramètres invalides\n");
        return -1;
    }
    
    int fd = fs_open(filename, O_CREAT | O_WRONLY);
    if (fd == -1) {
        printf("echo: échec d'écriture dans '%s': %s\n", filename, strerror(errno));
        return -1;
    }
    
    ssize_t written = fs_write(fd, text, strlen(text));
    if (written < 0) {
        printf("echo: échec d'écriture dans '%s'\n", filename);
        fs_close(fd);
        return -1;
    }
    
    ssize_t nl_written = fs_write(fd, "\n", 1);
    if (nl_written < 0) {
        printf("echo: avertissement, saut de ligne non écrit\n");
    }
    
    fs_close(fd);
    printf("Texte écrit avec succès dans '%s' (%zd octets)\n", filename, written + nl_written);
    return 0;
}

// Copie un fichier source vers une destination.
int fs_copy(const char *source, const char *dest) {
    int src_fd = fs_open(source, O_RDONLY);
    if (src_fd == -1) return -1;
   
    int dest_fd = fs_open(dest, O_CREAT | O_WRONLY | O_TRUNC);
    if (dest_fd == -1) {
        fs_close(src_fd);
        return -1;
    }
   
    char buffer[BLOCK_SIZE];
    ssize_t bytes;
    while ((bytes = fs_read(src_fd, buffer, sizeof(buffer))) > 0) {
        if (fs_write(dest_fd, buffer, bytes) != bytes) {
            fs_close(src_fd);
            fs_close(dest_fd);
            return -1;
        }
    }
   
    fs_close(src_fd);
    fs_close(dest_fd);
    return 0;
}

// Lance un éditeur basique pour modifier un fichier.
void fs_edit(const char *filename) {
    // 1. Ouvrir le fichier
    int fd = fs_open(filename, O_RDWR);
    if (fd == -1) {
        printf("edit: %s: %s\n", filename, strerror(errno));
        return;
    }

    // 2. Lire tout le contenu actuel
    char *content = NULL;
    size_t content_size = 0;
    char buffer[BLOCK_SIZE];
    ssize_t bytes_read;
    
    // Correction de la syntaxe de la boucle while
    while ((bytes_read = fs_read(fd, buffer, sizeof(buffer))) > 0) {
        char *new_content = realloc(content, content_size + bytes_read + 1);
        if (!new_content) {
            printf("Erreur mémoire\n");
            free(content);
            fs_close(fd);
            return;
        }
        content = new_content;
        memcpy(content + content_size, buffer, bytes_read);
        content_size += bytes_read;
    }

    if (content) {
        content[content_size] = '\0';
    } else {
        content = strdup("");
        content_size = 0;
    }

    // 3. Créer un fichier temporaire pour l'édition
    char temp_filename[] = "/tmp/fsedit_XXXXXX";
    int temp_fd = mkstemp(temp_filename);
    if (temp_fd == -1) {
        printf("Erreur création fichier temporaire\n");
        free(content);
        fs_close(fd);
        return;
    }

    // Écrire le contenu dans le fichier temporaire
    if (write(temp_fd, content, content_size) != (ssize_t)content_size) {
        printf("Erreur écriture temporaire\n");
        close(temp_fd);
        free(content);
        fs_close(fd);
        return;
    }
    close(temp_fd);

    // 4. Ouvrir l'éditeur système
    char editor_cmd[256];
    const char *editor = getenv("EDITOR");
    if (!editor) editor = "nano";
    
    snprintf(editor_cmd, sizeof(editor_cmd), "%s %s", editor, temp_filename);
    int ret = system(editor_cmd);
    
    if (ret != 0) {
        printf("Édition annulée\n");
        unlink(temp_filename);
        free(content);
        fs_close(fd);
        return;
    }

    // 5. Lire le contenu modifié
    FILE *temp_file = fopen(temp_filename, "r");
    if (!temp_file) {
        printf("Erreur lecture fichier temporaire\n");
        free(content);
        fs_close(fd);
        return;
    }

    fseek(temp_file, 0, SEEK_END);
    long new_size = ftell(temp_file);
    fseek(temp_file, 0, SEEK_SET);

    char *new_content = malloc(new_size + 1);
    if (!new_content) {
        printf("Erreur mémoire\n");
        fclose(temp_file);
        free(content);
        fs_close(fd);
        return;
    }

    size_t read_size = fread(new_content, 1, new_size, temp_file);
    new_content[read_size] = '\0';
    fclose(temp_file);
    unlink(temp_filename);

    // 6. Sauvegarder les modifications
    fs_lseek(fd, 0, SEEK_SET);
    
    // Tronquer si nécessaire
    if (read_size < content_size) {
        fs_truncate(fd, read_size);
    }
    
    // Écrire le nouveau contenu
    if (fs_write(fd, new_content, read_size) == -1) {
        printf("Erreur écriture: %s\n", strerror(errno));
    } else {
        printf("Sauvegardé %zu octets dans %s\n", read_size, filename);
    }

    // 7. Nettoyage
    free(content);
    free(new_content);
    fs_close(fd);
    verify_fs();
}

// Tronque ou agrandit un fichier à une nouvelle taille.
void fs_truncate(int fd, off_t new_size) {
    if (fd < 0 || fd >= MAX_FILES || open_files[fd].inode_idx == -1) {
        errno = EBADF;
        return;
    }

    int inode_idx = open_files[fd].inode_idx;
    if (fs.inodes[inode_idx].is_directory || fs.inodes[inode_idx].is_symlink) {
        errno = EISDIR;
        return;
    }

    // Si le fichier s'agrandit, juste mettre à jour la taille
    if (new_size >= fs.inodes[inode_idx].size) {
        fs.inodes[inode_idx].size = new_size;
        return;
    }

    // Si le fichier rétrécit, libérer les blocs excédentaires
    int blocks_needed = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int current_block = fs.inodes[inode_idx].first_block;
    int prev_block = -1;
    int block_count = 0;

    // Parcourir la chaîne de blocs
    while (current_block != -1) {
        if (++block_count > blocks_needed) {
            // Libérer ce bloc et les suivants
            int next_block = fs.blocks[current_block].next;
            
            if (prev_block == -1) {
                fs.inodes[inode_idx].first_block = -1;
            } else {
                fs.blocks[prev_block].next = -1;
            }
            
            fs.free_blocks[current_block] = 0;
            fs.free_block_count++;
            current_block = next_block;
        } else {
            prev_block = current_block;
            current_block = fs.blocks[current_block].next;
        }
    }

    // Mettre à jour la taille
    fs.inodes[inode_idx].size = new_size;
    fs.inodes[inode_idx].modification_time = time(NULL);
}
int resolve_path(const char *path, int current_dir, int *parent_dir, char *filename) {
    char temp_path[MAX_PATH];
    strncpy(temp_path, path, MAX_PATH-1);
    temp_path[MAX_PATH-1] = '\0';

    // Initialisation du parent_dir selon si le chemin est absolu ou relatif
    *parent_dir = (path[0] == '/') ? 0 : current_dir;

    char *token = strtok(temp_path, "/");
    char *last_token = NULL;

    // Cas spécial: chemin vide ou "."
    if (token == NULL || strcmp(token, ".") == 0) {
        filename[0] = '\0'; // Indique qu'on veut le répertoire lui-même
        return 0;
    }

    while (token != NULL) {
        // Gestion des ".."
        if (strcmp(token, "..") == 0) {
            if (*parent_dir != 0) { // Si pas déjà à la racine
                *parent_dir = fs.inodes[*parent_dir].parent_dir;
            }
            token = strtok(NULL, "/");
            continue;
        }

        // Gestion des "." (on ignore)
        if (strcmp(token, ".") == 0) {
            token = strtok(NULL, "/");
            continue;
        }

        last_token = token;
        token = strtok(NULL, "/");

        if (token != NULL) { // Ce n'est pas encore le dernier token
            int dir = find_inode_by_name(last_token, *parent_dir);
            if (dir == -1 || !fs.inodes[dir].is_directory) {
                errno = ENOENT;
                return -1;
            }
            *parent_dir = dir;
        }
    }

    if (last_token != NULL) {
        strncpy(filename, last_token, MAX_FILENAME-1);
        filename[MAX_FILENAME-1] = '\0';
    } else {
        filename[0] = '\0'; // Cas où le chemin se termine par '/'
    }

    return 0;
}

// Déplace ou renomme un fichier ou dossier.
int fs_move(const char *source, const char *dest, int current_dir) {
    int src_parent, dest_parent;
    char src_name[MAX_FILENAME], dest_name[MAX_FILENAME];

    // Résolution du chemin source
    if (resolve_path(source, current_dir, &src_parent, src_name) == -1) {
        fprintf(stderr, "mv: cannot stat '%s'\n", source);
        return -1;
    }

    int src_inode = find_inode_by_name(src_name, src_parent);
    if (src_inode == -1) {
        fprintf(stderr, "mv: cannot stat '%s'\n", source);
        return -1;
    }

    // Résolution du chemin destination
    if (resolve_path(dest, current_dir, &dest_parent, dest_name) == -1) {
        fprintf(stderr, "mv: cannot create at '%s'\n", dest);
        return -1;
    }

    // Cas spécial : destination est un répertoire existant
    int dest_inode = find_inode_by_name(dest_name, dest_parent);
    if (dest_inode != -1 && fs.inodes[dest_inode].is_directory) {
        dest_parent = dest_inode;
        strncpy(dest_name, src_name, MAX_FILENAME-1);
    }

    // Vérification des conflits
    if (find_inode_by_name(dest_name, dest_parent) != -1) {
        fprintf(stderr, "mv: cannot overwrite '%s'\n", dest);
        return -1;
    }

    // Mise à jour de la structure
    fs.inodes[src_inode].parent_dir = dest_parent;
    strncpy(fs.inodes[src_inode].filename, dest_name, MAX_FILENAME-1);
    fs.inodes[src_inode].modification_time = time(NULL);

    // Mise à jour de l'entrée dans le répertoire parent
    remove_dir_entry(src_parent, src_name);
    add_dir_entry(dest_parent, src_inode);

    return 0;
}

// Supprime une entrée de répertoire par son nom.
void remove_dir_entry(int parent, const char *name) {
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (fs.inodes[parent].entries[i] == find_inode_by_name(name, parent)) {
            fs.inodes[parent].entries[i] = -1;
            break;
        }
    }
}

// Ajoute une entrée dans un répertoire parent pour un nouvel inode.
void add_dir_entry(int parent, int inode) {
    for (int i = 0; i < MAX_DIR_ENTRIES; i++) {
        if (fs.inodes[parent].entries[i] == -1) {
            fs.inodes[parent].entries[i] = inode;
            break;
        }
    }
}


// Affiche les commandes disponibles et leur usage.
void show_help() {
    printf("\033[1;36m========== SYSTÈME DE FICHIERS UNIX SIMPLIFIÉ ==========\033[0m\n");
    printf("\033[1;33mCommandes disponibles (ordre alphabétique):\033[0m\n");
    printf("  \033[1mbackup\033[0m                  - Crée un point de restauration\n");
    printf("  \033[1mrmbackup <num>\033[0m          - Supprime un backup spécifique\n");
    printf("  \033[1mbackups\033[0m                 - Liste les sauvegardes disponibles\n");
    printf("  \033[1mcat <fichier>\033[0m           - Affiche le contenu d'un fichier\n");
    printf("  \033[1mcd <chemin>\033[0m             - Change de répertoire\n");
    printf("  \033[1mchmod <mode> <fichier>\033[0m  - Change les permissions (ex: 755)\n");
    printf("  \033[1mcp <source> <dest>\033[0m      - Copie un fichier\n");
    printf("  \033[1mdebug\033[0m                   - Affiche les infos de débogage\n");
    printf("  \033[1mdefrag\033[0m                  - Défragmente le système de fichiers\n");
    printf("  \033[1mecho <texte> > <fichier>\033[0m- Écrit du texte dans un fichier\n");
    printf("  \033[1medit <fichier>\033[0m          - Édite un fichier avec un éditeur externe\n");
    printf("  \033[1mexit\033[0m                    - Quitte le programme\n");
    printf("  \033[1mformat\033[0m                  - Formate le système de fichiers\n");
    printf("  \033[1mhelp\033[0m                    - Affiche cette aide\n");
    printf("  \033[1mln <cible> <lien>\033[0m       - Crée un lien physique\n");
    printf("  \033[1mln -s <cible> <lien>\033[0m    - Crée un lien symbolique\n");
    printf("  \033[1mls [chemin]\033[0m             - Liste le contenu du répertoire\n");
    printf("  \033[1mls -l [chemin]\033[0m          - Liste détaillée du contenu\n");
    printf("  \033[1mmkdir <répertoire>\033[0m      - Crée un répertoire\n");
    printf("  \033[1mmv <source> <dest>\033[0m      - Déplace/renomme un fichier\n");
    printf("  \033[1mpwd\033[0m                     - Affiche le répertoire courant\n");
    printf("  \033[1mrestore <index>\033[0m         - Restaure une version précédente\n");
    printf("  \033[1mrm <fichier>\033[0m            - Supprime un fichier\n");
    printf("  \033[1mrmdir <répertoire>\033[0m      - Supprime un répertoire vide\n");
    printf("  \033[1msave\033[0m                    - Sauvegarde l'état actuel\n");
    printf("  \033[1mstats\033[0m                   - Affiche les statistiques\n");
    printf("  \033[1mtouch <fichier>\033[0m         - Crée un fichier vide\n");
    printf("  \033[1mtree [chemin]\033[0m           - Affiche l'arborescence des fichiers\n");
    printf("  \033[1mtree --inodes [chemin]\033[0m  - Affiche avec numéros d'inodes\n");
    printf("\033[1;36m=======================================================\033[0m\n");
}

// Analyse une ligne de commande en arguments séparés.
int parse_args(char *input, char *args[]) {
    int count = 0;
    char *token = strtok(input, " \t\n");
    
    while (token != NULL && count < MAX_ARGS) {
        args[count++] = token;
        token = strtok(NULL, " \t\n");
    }
    
    args[count] = NULL;
    return count;
}

// Affiche des statistiques sur le système de fichiers (taille, utilisation, etc.).
void fs_stats() {
    int files_count = 0;
    int dirs_count = 0;
    int symlinks_count = 0;
    int used_blocks = 0;
    int used_inodes = 0;
    int total_size = 0;
    
    // Compter les blocs utilisés
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (fs.free_blocks[i] == 1) {
            used_blocks++;
        }
    }
    
    // Compter les inodes
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i]) {
            used_inodes++;
            if (fs.inodes[i].is_directory) {
                dirs_count++;
            } else if (fs.inodes[i].is_symlink) {
                symlinks_count++;
            } else {
                files_count++;
                total_size += fs.inodes[i].size;
            }
        }
    }
    
    printf("\033[1;36m===== STATISTIQUES DU SYSTÈME DE FICHIERS =====\033[0m\n");
    printf("Fichiers               : %d\n", files_count);
    printf("Répertoires            : %d\n", dirs_count);
    printf("Liens symboliques      : %d\n", symlinks_count);
    printf("Blocs utilisés         : %d / %d (%.1f%%)\n", 
           used_blocks, MAX_BLOCKS, (float)used_blocks / MAX_BLOCKS * 100);
    printf("Inodes utilisés        : %d / %d (%.1f%%)\n", 
           used_inodes, MAX_FILES, (float)used_inodes / MAX_FILES * 100);
    printf("Espace utilisé         : %d octets\n", total_size);
    printf("Taille moyenne fichier : %d octets\n", files_count > 0 ? total_size / files_count : 0);
    printf("\033[1;36m===========================================\033[0m\n");
}

// Lance une défragmentation complète.
void defragment() {
    printf("\nDébut de la défragmentation...\n");
    int used_blocks[MAX_BLOCKS] = {0};
    int block_map[MAX_BLOCKS] = {0};
    int new_block = 0;

    // 1. Identifier tous les blocs utilisés
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && !fs.inodes[i].is_directory && !fs.inodes[i].is_symlink) {
            int block = fs.inodes[i].first_block;
            while (block != -1) {
                used_blocks[block] = 1;
                block = fs.blocks[block].next;
            }
        }
    }

    // 2. Compacter les blocs utilisés
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (used_blocks[i]) {
            if (i != new_block) {
                // Déplacer le contenu du bloc
                memcpy(&fs.blocks[new_block], &fs.blocks[i], sizeof(Block));
            }
            block_map[i] = new_block++;
        }
    }

    // 3. Mettre à jour les pointeurs dans les inodes
    for (int i = 0; i < MAX_FILES; i++) {
        if (fs.free_inodes[i] && !fs.inodes[i].is_directory && !fs.inodes[i].is_symlink) {
            if (fs.inodes[i].first_block != -1) {
                fs.inodes[i].first_block = block_map[fs.inodes[i].first_block];
                
                int *block_ptr = &fs.inodes[i].first_block;
                while (*block_ptr != -1) {
                    *block_ptr = block_map[*block_ptr];
                    block_ptr = &fs.blocks[*block_ptr].next;
                }
            }
        }
    }

    // 4. Mettre à jour la table des blocs libres
    for (int i = 0; i < MAX_BLOCKS; i++) {
        fs.free_blocks[i] = (i < new_block) ? 1 : 0;
    }
    fs.free_block_count = MAX_BLOCKS - new_block;
    
    printf("Défragmentation terminée. %d blocs utilisés réorganisés.\n", new_block);
    verify_fs(); // Vérifier la cohérence après défragmentation
}


void debug_fs() {
    printf("\n=== DEBUG DU SYSTEME DE FICHIERS ===\n");
    
    // État global
    printf("Répertoire courant: '%s' (inode %d)\n", fs.current_path, fs.current_dir);
    printf("Blocs libres: %d/%d (%.1f%% utilisé)\n", 
          fs.free_block_count, MAX_BLOCKS, 
          (MAX_BLOCKS - fs.free_block_count) * 100.0 / MAX_BLOCKS);
    printf("Inodes libres: %d/%d (%.1f%% utilisé)\n", 
          fs.free_inode_count, MAX_FILES,
          (MAX_FILES - fs.free_inode_count) * 100.0 / MAX_FILES);
    
    // Fichiers ouverts
    printf("\nFichiers ouverts (%d/%d):\n", MAX_FILES - fs.free_inode_count, MAX_FILES);
    for (int i = 0; i < MAX_FILES; i++) {
        if (open_files[i].inode_idx != -1) {
            printf("FD %d: %s (pos: %ld, inode: %d)\n",
                  i, fs.inodes[open_files[i].inode_idx].filename,
                  open_files[i].position, open_files[i].inode_idx);
        }
    }
    
    // Détail des 10 premiers blocs
    printf("\nÉtat des 10 premiers blocs:\n");
    for (int i = 0; i < 10; i++) {
        printf("Bloc %d: %s", i, fs.free_blocks[i] ? "UTILISÉ" : "LIBRE");
        if (fs.free_blocks[i]) {
            printf(" (lié à: %d)", fs.blocks[i].next);
            
            // Trouver quel fichier utilise ce bloc
            for (int j = 0; j < MAX_FILES; j++) {
                if (fs.free_inodes[j] && fs.inodes[j].first_block == i) {
                    printf(" [%s]", fs.inodes[j].filename);
                    break;
                }
            }
        }
        printf("\n");
    }
    
    // Vérification de cohérence
    int computed_free_blocks = 0;
    int computed_free_inodes = 0;
    
    for (int i = 0; i < MAX_BLOCKS; i++) {
        if (!fs.free_blocks[i]) computed_free_blocks++;
    }
    
    for (int i = 0; i < MAX_FILES; i++) {
        if (!fs.free_inodes[i]) computed_free_inodes++;
    }
    
    printf("\nVérification de cohérence:\n");
    printf("Blocs libres: compteur=%d, calculé=%d | %s\n",
          fs.free_block_count, computed_free_blocks,
          fs.free_block_count == computed_free_blocks ? "OK" : "INCOHERENT");
          
    printf("Inodes libres: compteur=%d, calculé=%d | %s\n",
          fs.free_inode_count, computed_free_inodes,
          fs.free_inode_count == computed_free_inodes ? "OK" : "INCOHERENT");
    
    // Informations sur la fragmentation
    debug_fragmentation();
    
    printf("====================================\n");
}
/**
 * Affiche une analyse détaillée de la fragmentation
 */
// Affiche les informations de défragmentation pour le débogage.
 void debug_fragmentation() {
    int used_blocks = 0;    // Compteur de blocs utilisés
    int gaps = 0;           // Nombre de "trous" entre blocs utilisés
    int max_gap = 0;        // Taille maximale d’un trou
    int last_used = -1;     // Dernier bloc utilisé rencontré

     // Parcours de tous les blocs
    for (int i = 0; i < MAX_BLOCKS; i++) {  // Bloc utilisé
        if (fs.free_blocks[i] == 1) {
            used_blocks++;
            // Bloc utilisé
            if (last_used != -1 && i != last_used + 1) {
                gaps++; // Nouveau trou détecté
                int current_gap = i - last_used - 1;   // Taille du trou
                if (current_gap > max_gap) max_gap = current_gap;  // Mise à jour si c'est le plus grand
            }
            last_used = i;   // Mise à jour du dernier bloc utilisé
        }
    }

    // Affichage des statistiques de fragmentation
    printf("\n=== FRAGMENTATION ANALYSIS ===\n");
    printf("Used blocks: %d/%d\n", used_blocks, MAX_BLOCKS);
    printf("Gaps between blocks: %d\n", gaps);
    printf("Largest gap: %d blocks\n", max_gap);
    printf("Fragmentation rate: %.1f%%\n", 
          (last_used > 0) ? ((float)max_gap/last_used)*100 : 0);  // Estimation du taux de fragmentation

    // Suggestion selon les résultats
    if (gaps > 0) {
        printf("RECOMMENDATION: Compaction needed\n");  // Fragmentation détectée
    } else {
        printf("Status: No fragmentation detected\n");    // Aucun trou, bonne continuité
    }
}

// Fonction interne récursive utilisée pour afficher l’arborescence.
void print_tree(int current_inode, int depth, int *visited, int visited_count, int is_root) {
    // Ne pas afficher la racine en récursion
    if (depth > 0 && current_inode == 0) return;

    // Vérification des boucles (sauf pour la racine principale)
    if (!is_root) {
        for (int i = 0; i < visited_count; i++) {
            if (visited[i] == current_inode) {
                for (int i = 0; i < depth; i++) printf("    ");
                printf("└── [boucle] %s\n", fs.inodes[current_inode].filename);
                return;
            }
        }
    }

    // Ajout aux visités (sauf racine principale)
    if (!is_root) {
        visited[visited_count++] = current_inode;
    }

    // Indentation améliorée
    for (int i = 0; i < depth; i++) {
        printf(i < depth-1 ? "│   " : "    ");
    }

    // Affichage avec format unicode
    printf(depth > 0 ? "├── " : "");
    printf("%s", fs.inodes[current_inode].filename);

    // Gestion des liens symboliques
    if (fs.inodes[current_inode].is_symlink) {
        char *target = resolve_symlink(current_inode);
        if (target) {
            printf(" -> %s", target);
            free(target);
        }
    }
    printf("\n");

    // Parcours des enfants
    if (fs.inodes[current_inode].is_directory) {
        int children[MAX_FILES];
        int child_count = 0;
        
        // Collecte des enfants uniques
        for (int i = 0; i < MAX_FILES; i++) {
            if (fs.free_inodes[i] && fs.inodes[i].parent_dir == current_inode) {
                children[child_count++] = i;
            }
        }
        
        // Tri alphabétique
        for (int i = 0; i < child_count-1; i++) {
            for (int j = 0; j < child_count-i-1; j++) {
                if (strcmp(fs.inodes[children[j]].filename, fs.inodes[children[j+1]].filename) > 0) {
                    int temp = children[j];
                    children[j] = children[j+1];
                    children[j+1] = temp;
                }
            }
        }
        
        // Affichage récursif
        for (int i = 0; i < child_count; i++) {
            print_tree(children[i], depth + 1, visited, visited_count, 0);
        }
    }
}

void print_tree_with_inodes(int current_inode, int depth, int *visited, int visited_count, int is_root) {
    // Ne pas afficher la racine en récursion
    if (depth > 0 && current_inode == 0) return;

    // Vérification des boucles (sauf pour la racine principale)
    if (!is_root) {
        for (int i = 0; i < visited_count; i++) {
            if (visited[i] == current_inode) {
                for (int i = 0; i < depth; i++) printf("    ");
                printf("└── [%d] [boucle] %s\n", current_inode, fs.inodes[current_inode].filename);
                return;
            }
        }
    }

    // Ajout aux visités (sauf racine principale)
    if (!is_root) {
        visited[visited_count++] = current_inode;
    }

    // Indentation améliorée
    for (int i = 0; i < depth; i++) {
        printf(i < depth - 1 ? "│   " : "    ");
    }

    // Affichage avec format unicode
    printf(depth > 0 ? "├── " : "");
    printf("[%d] %s", current_inode, fs.inodes[current_inode].filename);

    // Gestion des liens symboliques
    if (fs.inodes[current_inode].is_symlink) {
        char *target = resolve_symlink(current_inode);
        if (target) {
            printf(" -> %s", target);
            free(target);
        }
    }
    printf("\n");

    // Parcours des enfants
    if (fs.inodes[current_inode].is_directory) {
        int children[MAX_FILES];
        int child_count = 0;

        // Collecte des enfants
        for (int i = 0; i < MAX_FILES; i++) {
            if (fs.free_inodes[i] && fs.inodes[i].parent_dir == current_inode) {
                children[child_count++] = i;
            }
        }

        // Tri alphabétique
        for (int i = 0; i < child_count - 1; i++) {
            for (int j = 0; j < child_count - i - 1; j++) {
                if (strcmp(fs.inodes[children[j]].filename, fs.inodes[children[j + 1]].filename) > 0) {
                    int temp = children[j];
                    children[j] = children[j + 1];
                    children[j + 1] = temp;
                }
            }
        }

        // Affichage récursif
        for (int i = 0; i < child_count; i++) {
            print_tree_with_inodes(children[i], depth + 1, visited, visited_count, 0);
        }
    }
}

// Affiche une vue arborescente du système de fichiers à partir d’un chemin.
void fs_tree(const char *path) {
    int target_inode;
    int visited_inodes[MAX_FILES] = {0};
    int visited_count = 0;
    
    if (path == NULL || strlen(path) == 0) {
        target_inode = fs.current_dir;
        printf(".\n");  // Affiche le répertoire courant
    } else {
        target_inode = find_inode_by_name(path, fs.current_dir);
        if (target_inode == -1) {
            printf("tree: '%s': Aucun fichier ou dossier de ce type\n", path);
            return;
        }
    }

    print_tree(target_inode, 0, visited_inodes, visited_count, 1);
}

// Affiche l’arborescence en montrant les numéros d’inodes.
void fs_tree_inodes(const char *path) {
    int target_inode;
    int visited_inodes[MAX_FILES] = {0};
    int visited_count = 0;

    if (path == NULL || strlen(path) == 0) {
        target_inode = fs.current_dir;
        printf("[%d] .\n", target_inode);  // Affiche le répertoire courant avec inode
    } else {
        target_inode = find_inode_by_name(path, fs.current_dir);
        if (target_inode == -1) {
            printf("tree: '%s': Aucun fichier ou dossier de ce type\n", path);
            return;
        }
    }

    print_tree_with_inodes(target_inode, 0, visited_inodes, visited_count, 1);
}

// Lance le shell interactif du système de fichiers.
void shell() {
    char input[MAX_COMMAND_LENGTH];
    char *args[MAX_ARGS + 1];
    int running = 1;

        // Seul endroit où appeler init_fs():
        if (!initialized) {
            init_fs();
        }
   
    printf("\033[1;32m=== Système de fichiers UNIX simplifié - Interface Shell ===\033[0m\n");
    printf("Tapez '\033[1mhelp\033[0m' pour voir les commandes disponibles\n\n");
   
    if (access("filesystem.dat", F_OK) != -1) {
        load_fs("filesystem.dat");
    } else {
        init_fs();
        printf("Nouveau système de fichiers créé (aucune sauvegarde trouvée)\n");
    }
   
    while (running) {
        printf("\033[0;32m%s\033[0m$ ", fs.current_path);
        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }
       
        if (input[strlen(input) - 1] == '\n') {
            input[strlen(input) - 1] = '\0';
        }
       
        if (strlen(input) == 0) {
            continue;
        }
       
        char *redirect = strstr(input, " > ");
        if (redirect != NULL) {
            *redirect = '\0';
            char *filename = redirect + 3;
            while (*filename == ' ') filename++;
           
            if (strncmp(input, "echo ", 5) == 0) {
                char *text = input + 5;
                if (fs_echo(text, filename) == -1) {
                    printf("echo: %s: %s\n", filename, strerror(errno));
                }
                continue;
            }
        }
       
        int argc = parse_args(input, args);
        if (argc == 0) continue;
       
        if (strcmp(args[0], "ls") == 0) {
            if (argc > 1 && strcmp(args[1], "-l") == 0) {
                fs_ls_l(argc > 2 ? args[2] : NULL);  // Appel de ls -l
            } else {
                fs_ls(argc > 1 ? args[1] : NULL);   // Appel de ls classique
            }
        }
        else if (strcmp(args[0], "mkdir") == 0) {
            int recursive = 0;
            const char *path = NULL;
            mode_t mode = 0755; // Permissions par défaut
            
            // Analyse des arguments
            for (int i = 1; i < argc; i++) {
                if (strcmp(args[i], "-p") == 0) {
                    recursive = 1;
                } else if (strcmp(args[i], "-m") == 0 && i+1 < argc) {
                    mode = strtol(args[++i], NULL, 8); // Permissions en octal
                } else {
                    path = args[i];
                }
            }
            
            if (path == NULL) {
                printf("Utilisation: mkdir [-p] [-m mode] <nom>\n");
            } else if (fs_mkdir(path, mode, recursive) == -1) {
                printf("mkdir: %s: %s\n", path, strerror(errno));
            }
        } else if (strcmp(args[0], "rmdir") == 0) {
            if (argc != 2) {
                printf("Utilisation: rmdir <nom>\n");
            } else if (fs_rmdir(args[1]) == -1) {
                printf("rmdir: %s: %s\n", args[1], strerror(errno));
            }
        } else if (strcmp(args[0], "cp") == 0) {
            if (argc != 3) {
                printf("Utilisation: cp <source> <destination>\n");
            } else if (fs_copy(args[1], args[2]) == -1) {
                printf("cp: %s: %s\n", args[2], strerror(errno));
            }
        }
        else if (strcmp(args[0], "cd") == 0) {
            if (argc != 2) {
                printf("Utilisation: cd <chemin>\n");
            } else {
                if (fs_chdir(args[1]) == -1) {
                    printf("cd: %s: %s\n", args[1], strerror(errno));
                }
            }
        
        } else if (strcmp(args[0], "pwd") == 0) {
            char cwd[MAX_PATH];
            if (fs_getcwd(cwd, sizeof(cwd))) {
                printf("%s\n", cwd);
            } else {
                printf("pwd: %s\n", strerror(errno));
            }
        } else if (strcmp(args[0], "defrag") == 0) {
            printf("La défragmentation est maintenant automatique.\n");
        }
        else if (strcmp(args[0], "touch") == 0) {
            if (argc != 2) {
                printf("Utilisation: touch <nom>\n");
            } else if (fs_touch(args[1]) == -1) {
                printf("touch: %s: %s\n", args[1], strerror(errno));
            }
        } else if (strcmp(args[0], "cat") == 0) {
            if (argc != 2) {
                printf("Utilisation: cat <nom>\n");
            } else {
                fs_cat(args[1]);
            }
        } else if (strcmp(args[0], "rm") == 0) {
            if (argc != 2) {
                printf("Utilisation: rm <nom>\n");
            } else if (fs_unlink(args[1]) == -1) {
                printf("rm: %s: %s\n", args[1], strerror(errno));
            }
        } else if (strcmp(args[0], "chmod") == 0) {
            if (argc != 3) {
                printf("Utilisation: chmod <mode> <nom>\n");
            } else {
                mode_t mode = strtol(args[1], NULL, 8);
                if (fs_chmod(args[2], mode) == -1) {
                    printf("chmod: %s: %s\n", args[2], strerror(errno));
                }
            }
        } else if (strcmp(args[0], "edit") == 0) {
            if (argc != 2) {
                printf("Utilisation: edit <fichier>\n");
            } else {
                fs_edit(args[1]);
            }
        }
        else if (strcmp(args[0], "mv") == 0) {
            if (argc != 3) {
                printf("Utilisation: mv <source> <destination>\n");
            } else if (fs_move(args[1], args[2], fs.current_dir) == -1) {
                printf("mv: %s: %s\n", args[2], strerror(errno));
            }
        }
        else if (strcmp(args[0], "debug") == 0) {
            debug_fs();
        }
         else if (strcmp(args[0], "ln") == 0) {
            if (argc == 4 && strcmp(args[1], "-s") == 0) {
                if (fs_symlink(args[2], args[3]) == -1) {
                    printf("ln: %s: %s\n", args[3], strerror(errno));
                }
            } else if (argc == 3) {
                if (fs_link(args[1], args[2]) == -1) {
                    printf("ln: %s: %s\n", args[2], strerror(errno));
                }
            } else if (strcmp(args[0], "defrag") == 0) {
                defragment();
                printf("Défragmentation terminée.\n");
            }
            else {
                printf("Utilisation:\n");
                printf("  ln -s <cible> <lien>  # Crée un lien symbolique\n");
                printf("  ln <cible> <lien>     # Crée un lien physique\n");
            }
        } else if (strcmp(args[0], "format") == 0) {
            char confirm[10];
            printf("\033[1;31mATTENTION: Cette opération effacera toutes les données!\033[0m\n");
            printf("Tapez 'OUI' pour confirmer: ");
            if (fgets(confirm, sizeof(confirm), stdin)) {
                if (strncmp(confirm, "OUI\n", 4) == 0) {
                    format_fs();
                } else {
                    printf("Formatage annulé\n");
                }
            }
        } else if (strcmp(args[0], "save") == 0) {
            save_fs("filesystem.dat");
        } else if (strcmp(args[0], "stats") == 0) {
            fs_stats();
        } else if (strcmp(args[0], "exit") == 0) {
            running = 0;
        } else if (strcmp(args[0], "help") == 0) {
            show_help();
        }  else if (strcmp(args[0], "tree") == 0) {
            int show_inodes = 0;
            const char *path = NULL;
            
            // Analyse des arguments
            for (int i = 1; i < argc; i++) {
                if (strcmp(args[i], "--inodes") == 0 || strcmp(args[i], "-i") == 0) {
                    show_inodes = 1;
                } else {
                    path = args[i];
                }
            }
            
            if (argc > 3) {
                printf("Utilisation: tree [--inodes|-i] [chemin]\n");
            } else {
                if (show_inodes) {
                    fs_tree_inodes(path);
                } else {
                    fs_tree(path);
                }
            }
        }
        // Ajouter dans la liste des commandes reconnues
else if (strcmp(args[0], "backup") == 0) {
    create_backup();
    printf("Backup créé avec succès (total: %d/%d)\n", backup_count, MAX_BACKUPS);
} 
else if (strcmp(args[0], "backups") == 0) {
    list_backups();
} 
else if (strcmp(args[0], "restore") == 0) {
    if (argc != 2) {
        printf("Utilisation: restore <index>\n");
        list_backups();
    } else {
        int index = atoi(args[1]);
        if (restore_backup(index) == 0) {
            printf("Restauration réussie!\n");
        } else {
            printf("Erreur lors de la restauration. Utilisez 'backups' pour voir la liste.\n");
        }
     }
    }
    else if (strcmp(args[0], "rmbackup") == 0) {
        if (argc != 2) {
            printf("Usage: rmbackup <numéro>\n");
            list_backups();
        } else {
            int backup_num = atoi(args[1]);
            remove_single_backup(backup_num);
        }
    }
        else {
            printf("\033[1;31m%s: commande introuvable\033[0m\n", args[0]);
            printf("Tapez 'help' pour voir les commandes disponibles\n");
        }
    }
   
    save_fs("filesystem.dat");
    printf("Au revoir! Le système de fichiers a été sauvegardé\n");
}
