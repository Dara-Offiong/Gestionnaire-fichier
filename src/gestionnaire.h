/*
 * Fichier : gestionnaire.c
 * Auteurs :
 *  - MEZIANE Fella - 33.33%
 *  - BOUKILI Inas - 33.33%
  * - OFFIONG Dara - 33.33%
 * Date de création : 15/02/2025
 * Dernière modification : 05/04/2025
 */
#ifndef GESTIONNAIRE_H
#define GESTIONNAIRE_H

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define BLOCK_SIZE 1024
#define MAX_FILENAME 128
#define MAX_PATH 1024
#define MAX_BLOCKS 1024
#define MAX_FILES 64
#define MAX_DIRS 32
#define MAX_COMMAND_LENGTH 256
#define MAX_ARGS 10
#define MAX_SYMLINK_DEPTH 10
#define DEBUG 1  // Mettre à 0 pour désactiver
#define MAX_DIR_ENTRIES 32
#define MAX_BACKUPS 5
#define MAX_INODES 256 
typedef struct {
    char data[BLOCK_SIZE];
    int next;
} Block;

typedef struct {
    char filename[MAX_FILENAME];
    int is_directory;
    int is_symlink;
    int first_block;
    int size;
    int parent_dir;
    mode_t permissions;
    time_t creation_time;
    time_t modification_time;
    int link_count;
    int entries[MAX_DIR_ENTRIES];  // Tableau des inodes des entrées
    int entry_count;
    int num_entries;
} Inode;

typedef struct {
    Block blocks[MAX_BLOCKS];
    Inode inodes[MAX_FILES];
    int free_blocks[MAX_BLOCKS];
    int free_inodes[MAX_FILES];
    int current_dir;
    char current_path[MAX_PATH];
    int free_block_count;
    int free_inode_count;
} FileSystem;

typedef struct {
    int inode_idx;
    off_t position;
} FileDescriptor;

extern FileSystem fs;
extern int initialized;
extern FileDescriptor open_files[MAX_FILES];
extern FileSystem backups[MAX_BACKUPS];
extern time_t backup_times[MAX_BACKUPS];
extern int backup_count;

//Gestion du système de fichiers

void init_fs(); 
// Initialise le système de fichiers en mémoire.

int find_free_block(); 
// Trouve et retourne l’index d’un bloc libre dans le système de fichiers.

int find_free_inode(); 
// Trouve et retourne l’index d’un inode libre.

int find_inode_by_name(const char *name, int dir_inode); 
// Cherche un fichier ou dossier par nom dans un répertoire donné.

void save_fs(const char *filename); 
// Sauvegarde l’état actuel du système de fichiers dans un fichier.

void load_fs(const char *filename); 
// Charge un système de fichiers sauvegardé depuis un fichier.

void format_fs(); 
// Réinitialise le système de fichiers (formattage complet).

//Gestion des fichiers
int fs_open(const char *filename, int flags); 
// Ouvre un fichier (lecture, écriture, etc.) et retourne un descripteur de fichier.

int fs_close(int fd); 
// Ferme un fichier ouvert identifié par son descripteur.

ssize_t fs_read(int fd, void *buf, size_t count); 
// Lit des données depuis un fichier ouvert dans un tampon.

ssize_t fs_write(int fd, const void *buf, size_t count); 
// Écrit des données dans un fichier ouvert depuis un tampon.

off_t fs_lseek(int fd, off_t offset, int whence); 
// Change la position de lecture/écriture dans un fichier (seek).

void fs_truncate(int fd, off_t new_size); 
// Tronque ou agrandit un fichier à une nouvelle taille.


//Gestion des répertoires
int fs_mkdir(const char *path, mode_t mode, int recursive); 
// Crée un répertoire (optionnellement de manière récursive).

int fs_rmdir(const char *path); 
// Supprime un répertoire (vide).

char *fs_getcwd(char *buf, size_t size); 
// Retourne le chemin du répertoire de travail courant.

int fs_chdir(const char *path); 
// Change le répertoire de travail courant.

void add_dir_entry(int parent, int inode); 
// Ajoute une entrée dans un répertoire parent pour un nouvel inode.

void remove_dir_entry(int parent, const char *name); 
// Supprime une entrée de répertoire par son nom.


//Opérations système
int fs_unlink(const char *path); 
// Supprime un fichier (unlink).

int fs_chmod(const char *path, mode_t mode); 
// Change les permissions d’un fichier ou dossier.

int fs_link(const char *oldpath, const char *newpath); 
// Crée un lien physique vers un fichier existant.

int fs_symlink(const char *target, const char *linkpath); 
// Crée un lien symbolique vers une cible.

char *resolve_symlink(int inode_idx); 
// Résout et retourne la cible d’un lien symbolique.

//Commandes utilitaires (shell)
void fs_ls(const char *path); 
// Affiche le contenu d’un répertoire (comme `ls`).

void fs_ls_l(const char *path); 
// Affiche les détails étendus d’un répertoire (comme `ls -l`).

void fs_cat(const char *filename); 
// Affiche le contenu d’un fichier (comme `cat`).

int fs_touch(const char *filename); 
// Crée un fichier vide ou met à jour sa date de modification (comme `touch`).

int fs_echo(const char *text, const char *filename); 
// Écrit du texte dans un fichier (comme `echo "..." > file`).

int fs_copy(const char *source, const char *dest); 
// Copie un fichier source vers une destination.

int fs_move(const char *source, const char *dest, int current_dir); 
// Déplace ou renomme un fichier ou dossier.

void fs_edit(const char *filename); 
// Lance un éditeur basique pour modifier un fichier.

void show_help(); 
// Affiche les commandes disponibles et leur usage.

int parse_args(char *input, char *args[]); 
// Analyse une ligne de commande en arguments séparés.

void shell(); 
// Lance le shell interactif du système de fichiers.

void fs_stats(); 
// Affiche des statistiques sur le système de fichiers (taille, utilisation, etc.).

void print_file_details_long(int inode_idx); 
// Affiche les détails d’un fichier de manière détaillée (`ls -l` par inode).


//Affichage en arbre
void fs_tree(const char *path); 
// Affiche une vue arborescente du système de fichiers à partir d’un chemin.

void fs_tree_inodes(const char *path); 
// Affiche l’arborescence en montrant les numéros d’inodes.

void print_tree(int current_inode, int depth, int *visited, int visited_count, int is_root); 
// Fonction interne récursive utilisée pour afficher l’arborescence.

//Gestion de la fragmentation
int calculate_fragmentation(); 
// Calcule le taux de fragmentation du système de fichiers.

void auto_defragment_if_needed(); 
// Lance une défragmentation automatique si nécessaire.

void defragment(); 
// Lance une défragmentation complète.

void defragment_light(); 
// Lance une défragmentation partielle (moins agressive).

void relocate_block(int old_block, int new_block); 
// Déplace un bloc de données à un nouvel emplacement.

void compact_blocks(int freed_block); 
// Réorganise les blocs pour réduire les espaces vides.

void debug_fragmentation(); 
// Affiche les informations de défragmentation pour le débogage.

//Sauvegarde et restauration
void create_backup(); 
// Crée une sauvegarde complète du système de fichiers en mémoire.

void list_backups(); 
// Liste toutes les sauvegardes disponibles avec leurs dates.

int restore_backup(int index); 
// Restaure une sauvegarde à partir de son index.

//Lié aux hardlinks (liens physiques)
int add_entry_to_dir(int dir_inode, int new_inode); 
// Ajoute une entrée dans un répertoire pour un fichier (inode).

void update_all_hardlinks(int inode_num, size_t new_size, time_t mtime); 
// Met à jour les métadonnées de tous les liens physiques d’un même inode.


#endif