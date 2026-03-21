## WAL Reader

### Descripción general

Este documento proporciona una descripción general de la implementación del WAL (Write-Ahead Log) reader, enfocándose en las APIs internas e interfaces para desarrolladores. Para documentación orientada al usuario sobre las herramientas WAL, consulta el [capítulo de herramientas WAL](18-wal-tools.md).

El WAL reader proporciona la funcionalidad principal para parsear archivos PostgreSQL Write-Ahead Log, incluyendo soporte para archivos WAL encriptados y comprimidos. La implementación incluye tanto APIs de alto nivel para uso de aplicaciones como funciones de parseo de bajo nivel para procesamiento interno.


### Descripción general de la API de alto nivel

La siguiente sección proporciona una descripción general de cómo los usuarios pueden interactuar con las funciones y estructuras definidas en el archivo `walfile.h`. Estas APIs te permiten leer, escribir y gestionar archivos Write-Ahead Log (WAL).

**`struct walfile`**

El struct walfile representa la estructura principal usada para interactuar con archivos WAL en PostgreSQL. Un archivo WAL almacena un log de cambios a la base de datos y se usa para crash recovery, replicación y otros propósitos. Cada archivo WAL consta de páginas (cada 8192 bytes por defecto), conteniendo records que capturan cambios en la base de datos.

_Campos:_

- **magic_number**: Identifica la versión de PostgreSQL que creó el archivo WAL.
- **long_phd**: Un pointer a la extended header (long header) encontrada en la primera página del archivo WAL. Este header contiene metadatos adicionales.
- **page_headers**: Un deque de headers representando cada página en el archivo WAL, excluyendo la primera página.
- **records**: Un deque de WAL records decodificados. Cada record representa un cambio a la base de datos y contiene metadatos y los datos actuales a ser aplicados durante recovery o replicación.

**Descripción de funciones**

El archivo `walfile.h` proporciona tres funciones clave para interactuar con archivos WAL: `pgmoneta_read_walfile`, `pgmoneta_write_walfile`, y `pgmoneta_destroy_walfile`. Estas funciones te permiten leer desde, escribir a, y destruir objetos de archivo WAL, respectivamente.

**`pgmoneta_read_walfile`**

```c
int pgmoneta_read_walfile(int server, char* path, struct walfile** wf);
```

_Descripción:_

Esta función lee un archivo WAL desde una ruta específica y popula una estructura `walfile` con sus contenidos, incluyendo los headers y records del archivo.

_Parámetros:_

- **server**: El índice del servidor Postgres en la configuración de Pgmoneta.
- **path**: La ruta del archivo al archivo WAL que necesita ser leído.
- **wf**: Un pointer a un pointer a una estructura `walfile` que será poblada con los datos del archivo WAL.

_Retorna:_

- Retorna `0` en caso de éxito o `1` en caso de fallo.

_Ejemplo de uso:_

```c
struct walfile* wf = NULL;
int result = pgmoneta_read_walfile(0, "/path/to/walfile", &wf);
if (result == 0) {
    // Archivo WAL leído exitosamente
}
```

**`pgmoneta_write_walfile`**

```c
int pgmoneta_write_walfile(struct walfile* wf, int server, char* path);
```

_Descripción:_

Esta función escribe los contenidos de una estructura `walfile` de vuelta a disco, guardándola como un archivo WAL en la ruta especificada.

_Parámetros:_

- **wf**: El struct `walfile` conteniendo los datos WAL a ser escritos.
- **server**: El índice o ID del servidor donde el archivo WAL debe ser guardado.
- **path**: La ruta del archivo donde el archivo WAL debe ser escrito.

_Retorna:_

- Retorna `0` en caso de éxito o `1` en caso de fallo.

_Ejemplo de uso:_

```c
int result = pgmoneta_write_walfile(wf, 0, "/path/to/output_walfile");
if (result == 0)
{
    // Archivo WAL escrito exitosamente
}
```

**`pgmoneta_destroy_walfile`**

```c
void pgmoneta_destroy_walfile(struct walfile* wf);
```

_Descripción:_

Esta función libera la memoria asignada para una estructura `walfile`, incluyendo sus headers y records.

_Parámetros:_

- **wf**: El struct `walfile` a ser destruido.

_Ejemplo de uso:_

```c
struct walfile* wf = NULL;
int result = pgmoneta_read_walfile(0, "/path/to/walfile", &wf);
if (result == 0) {
    // Archivo WAL leído exitosamente
}
pgmoneta_destroy_walfile(wf);
```

**`pgmoneta_describe_walfile`**

```c
int pgmoneta_describe_walfile(char* path, enum value_type type, FILE* output, bool quiet, bool color,
                              struct deque* rms, uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                              uint32_t limit, bool summary, char** included_objects);
```

_Descripción:_

Esta función lee un único archivo WAL en la ruta especificada `path`, filtra sus records basándose en los parámetros proporcionados, y escribe la salida formateada a `output`.

_Parámetros:_

- **path**: Ruta al archivo WAL a ser descrito
- **type**: Tipo de formato de salida (raw o JSON)
- **output**: Stream de archivo para salida; si NULL, imprime a stdout
- **quiet**: Si es true, suprime salida detallada
- **color**: Si es true, habilita salida coloreada para mejor legibilidad
- **rms**: Deque de resource managers para filtrar
- **start_lsn**: LSN inicial para filtrar records (0 para sin filtro)
- **end_lsn**: LSN final para filtrar records (0 para sin filtro)
- **xids**: Deque de IDs de transacción para filtrar
- **limit**: Número máximo de records a mostrar (0 para sin límite)
- **summary**: Mostrar un resumen de conteos de WAL records agrupados por resource manager
- **included_objects**: Array de nombres de objetos para filtrar (NULL para todos)

_Retorna:_

- Retorna `0` en caso de éxito o `1` en caso de fallo.

**`pgmoneta_describe_walfiles_in_directory`**

```c
int pgmoneta_describe_walfiles_in_directory(char* dir_path, enum value_type type, FILE* output,
                                            bool quiet, bool color, struct deque* rms,
                                            uint64_t start_lsn, uint64_t end_lsn, struct deque* xids,
                                            uint32_t limit, bool summary, char** included_objects);
```

_Descripción:_

Esta función procesa todos los archivos WAL en el directorio especificado por `dir_path`, aplica la misma lógica de filtrado que `pgmoneta_describe_walfile`, y escribe resultados agregados a `output`.

_Parámetros:_

- **dir_path**: Ruta al directorio conteniendo archivos WAL
- **type**: Tipo de formato de salida (raw o JSON)
- **output**: Stream de archivo para salida; si NULL, imprime a stdout
- **quiet**: Si es true, suprime salida detallada
- **color**: Si es true, habilita salida coloreada para mejor legibilidad
- **rms**: Deque de resource managers para filtrar
- **start_lsn**: LSN inicial para filtrar records (0 para sin filtro)
- **end_lsn**: LSN final para filtrar records (0 para sin filtro)
- **xids**: Deque de IDs de transacción para filtrar
- **limit**: Número máximo de records a mostrar (0 para sin límite)
- **summary**: Mostrar un resumen de conteos de WAL records agrupados por resource manager
- **included_objects**: Array de nombres de objetos para filtrar (NULL para todos)

_Retorna:_

- Retorna `0` en caso de éxito o `1` en caso de fallo

### Descripción general de la API interna

**struct partial_xlog_record**

El struct partial_xlog_record representa un WAL XLOG record incompleto encontrado durante el parseo. Se usa para gestionar records que abarcan múltiples archivos WAL.

```c
struct partial_xlog_record
{
   char* data_buffer;                  /**< Porción de datos del record. */
   char* xlog_record;                  /**< Pointer al xlog record. */
   uint32_t data_buffer_bytes_read;    /**< Longitud del total de datos leídos en data_buffer. */
   uint32_t xlog_record_bytes_read;    /**< Longitud del total de datos leídos en xlog_record buffer. */
};
```

_Campos:_

- **data_buffer**: Contiene la porción de datos del WAL record parcialmente leído.
- **xlog_record**: Apunta a la estructura de header conteniendo metadatos sobre el WAL record.
- **data_buffer_bytes_read**: Longitud del total de datos leídos en data_buffer.
- **xlog_record_bytes_read**: Longitud del total de datos leídos en xlog_record buffer.

**`parse_wal_file`**

Esta función es responsable de leer y parsear un archivo PostgreSQL Write-Ahead Log (WAL).

_Parámetros_

- **path**: La ruta del archivo al archivo WAL que necesita ser parseado.
- **server_info**: Un pointer a una estructura `server` conteniendo información sobre el servidor.

_Descripción_

La función `parse_wal_file` abre el archivo WAL especificado por el parámetro `path` en modo binario y lee los WAL records. Procesa estos records, manejando varios casos como records que cruzan límites de página, mientras asegura correcta gestión de memoria a lo largo del proceso.

**Ejemplo de uso**

```c
parse_wal_file("/path/to/wal/file", &my_server);
```

**Estructura del archivo WAL**

La imagen ilustra la estructura de un archivo WAL (Write-Ahead Logging) en PostgreSQL, enfocándose en cómo los XLOG records se organizan dentro de segmentos WAL.

Fuente: [https://www.interdb.jp/pg/pgsql09/03.html](https://www.interdb.jp/pg/pgsql09/03.html)

![Estructura del archivo WAL](https://www.interdb.jp/pg/pgsql09/fig-9-07.png)

Un segmento WAL, por defecto, es un archivo de 16 MB, dividido en páginas de 8192 bytes (8 KB) cada una. La primera página contiene un header definido por la estructura XLogLongPageHeaderData, mientras que todas las páginas subsecuentes tienen headers descritos por la estructura XLogPageHeaderData. XLOG records se escriben secuencialmente en cada página, comenzando al inicio y moviéndose hacia abajo.

La figura destaca cómo el WAL asegura consistencia de datos escribiendo secuencialmente XLOG records en páginas, estructuradas dentro de mayores segmentos WAL de 16 MB.


### Resource Managers

En el contexto del WAL reader, los resource managers (rm) son responsables de manejar diferentes tipos de records encontrados dentro de un archivo WAL. Cada record en el archivo WAL está asociado con un resource manager específico, que determina cómo ese record es procesado.

**Definiciones de Resource Manager**

Cada resource manager es definido en el archivo header `rm_[name].h` e implementado en el archivo fuente correspondiente `rm_[name].c`.

En el archivo header `rmgr.h`, los resource managers se declaran como un enum, con cada resource manager teniendo un identificador único.

**Funciones de Resource Manager**

Cada resource manager implementa la función `rm_desc`, que proporciona una descripción del tipo de record asociado con ese resource manager. En el futuro, serán extendidos para implementar la función `rm_redo` que aplica los cambios a otro servidor.

**Soportando varias estructuras WAL en versiones de PostgreSQL 13 a 17**

La estructura WAL ha evolucionado a través de versiones de PostgreSQL 13 a 17, requiriendo manejo diferente para cada versión. Para acomodar estas diferencias, hemos implementado un approach basado en wrappers, como el factory pattern, para manejar estructuras WAL variables.

Abajo están los commit hashes para los magic values oficialmente soportados en cada versión de PostgreSQL:

1. PostgreSQL 13 - [0xD106][D106]
2. PostgreSQL 14 - [0xD10D][D10D]
3. PostgreSQL 15 - [0xD110][D110]
4. PostgreSQL 16 - [0xD113][D113]
5. PostgreSQL 17 - [0xD116][D116]
6. PostgreSQL 18 - [0xD118][D118]


`xl_end_of_recovery` es un ejemplo de cómo manejamos diferentes versiones de estructuras con un wrapper struct y el factory pattern.

```c
struct xl_end_of_recovery_v16 {
    timestamp_tz end_time;
    timeline_id this_timeline_id;
    timeline_id prev_timeline_id;
};

struct xl_end_of_recovery_v17 {
    timestamp_tz end_time;
    timeline_id this_timeline_id;
    timeline_id prev_timeline_id;
    int wal_level;
};

struct xl_end_of_recovery {
    int pg_version;
    union {
        struct xl_end_of_recovery_v16 v16;
        struct xl_end_of_recovery_v17 v17;
    } data;
    void (*parse)(struct xl_end_of_recovery* wrapper, void* rec);
    char* (*format)(struct xl_end_of_recovery* wrapper, char* buf);
};

xl_end_of_recovery* create_xl_end_of_recovery(int pg_version) {
    xl_end_of_recovery* wrapper = malloc(sizeof(xl_end_of_recovery));
    wrapper->pg_version = pg_version;

    if (pg_version >= 17) {
        wrapper->parse = parse_v17;
        wrapper->format = format_v17;
    } else {
        wrapper->parse = parse_v16;
        wrapper->format = format_v16;
    }

    return wrapper;
}

void parse_v16(xl_end_of_recovery* wrapper, void* rec) {
    memcpy(&wrapper->data.v16, rec, sizeof(struct xl_end_of_recovery_v16));
}

void parse_v17(xl_end_of_recovery* wrapper, void* rec) {
    memcpy(&wrapper->data.v17, rec, sizeof(struct xl_end_of_recovery_v17));
}

char* format_v16(xl_end_of_recovery* wrapper, char* buf) {
    struct xl_end_of_recovery_v16* xlrec = &wrapper->data.v16;
    return pgmoneta_format_and_append(buf, "tli %u; prev tli %u; time %s",
                                      xlrec->this_timeline_id, xlrec->prev_timeline_id,
                                      pgmoneta_wal_timestamptz_to_str(xlrec->end_time));
}

char* format_v17(xl_end_of_recovery* wrapper, char* buf) {
    struct xl_end_of_recovery_v17* xlrec = &wrapper->data.v17;
    return pgmoneta_format_and_append(buf, "tli %u; prev tli %u; time %s; wal_level %d",
                                      xlrec->this_timeline_id, xlrec->prev_timeline_id,
                                      pgmoneta_wal_timestamptz_to_str(xlrec->end_time),
                                      xlrec->wal_level);
}
```

**XID64 (64-bit Transaction IDs)**

pgmoneta funciona con patches externos de PostgreSQL que introducen 64-bit Transaction IDs (FullTransactionId).

No se requiere configuración especial para habilitar este soporte; es manejado automáticamente basándose en la versión de PostgreSQL detectada.


### WAL Change List

Esta sección lista los cambios en el formato WAL entre diferentes versiones de PostgreSQL.

**xl_clog_truncate**

17

```c
struct xl_clog_truncate
{
   int64 pageno;              /**< El page number del clog a truncar */
   transaction_id oldestXact;  /**< El transaction ID más antiguo a retener */
   oid oldestXactDb;        /**< El database ID de la transacción más antigua */
};
```

16

```c
struct xl_clog_truncate
{
   int64 pageno;              /**< El page number del clog a truncar */
   transaction_id oldestXact;  /**< El transaction ID más antiguo a retener */
   oid oldestXactDb;        /**< El database ID de la transacción más antigua */
};
```

**xl_commit_ts_truncate**

17:

```c
typedef struct xl_commit_ts_truncate
{
	int64		pageno;
	TransactionId oldestXid;
} xl_commit_ts_truncate;
```

16:

```c
typedef struct xl_commit_ts_truncate
{
	int			pageno;
	TransactionId oldestXid;
} xl_commit_ts_truncate;
```

**xl_heap_prune**

17:

```c
typedef struct xl_heap_prune
{
	uint8		reason;
	uint8		flags;

	/*
	 * Si XLHP_HAS_CONFLICT_HORIZON está set, el conflict horizon XID sigue,
	 * sin alineación
	 */
} xl_heap_prune;
#define SizeOfHeapPrune (offsetof(xl_heap_prune, flags) + sizeof(uint8))

```

16:

```c
typedef struct xl_heap_prune
{
	TransactionId snapshotConflictHorizon;
	uint16		nredirected;
	uint16		ndead;
	bool		isCatalogRel;	/* para manejar recovery conflict durante logical
								 * decoding en standby */
	/* OFFSET NUMBERS están en la block reference 0 */
} xl_heap_prune;
#define SizeOfHeapPrune (offsetof(xl_heap_prune, isCatalogRel) + sizeof(bool))

```

**xlhp_freeze_plan**

Removido `xl_heap_freeze_page`

17:

```c
typedef struct xlhp_freeze_plan
{
	TransactionId xmax;
	uint16		t_infomask2;
	uint16		t_infomask;
	uint8		frzflags;

	/* Longitud del individual page offset numbers array para este plan */
	uint16		ntuples;
} xlhp_freeze_plan;
```

**spgxlogState**

(No necesita ser cambiado)

17:

```c
typedef struct spgxlogState
{
	TransactionId redirectXid;
	bool		isBuild;
} spgxlogState;
```

16:

```c
typedef struct spgxlogState
{
	TransactionId myXid;
	bool		isBuild;
} spgxlogState;
```

**xl_end_of_recovery**

17:

```c
typedef struct xl_end_of_recovery
{
	TimestampTz end_time;
	TimeLineID	ThisTimeLineID; /* nuevo TLI */
	TimeLineID	PrevTimeLineID; /* anterior TLI del cual hacemos fork */
	int			wal_level;
} xl_end_of_recovery;
```

16:

```c
typedef struct xl_end_of_recovery
{
	TimestampTz end_time;
	TimeLineID	ThisTimeLineID; /* nuevo TLI */
	TimeLineID	PrevTimeLineID; /* anterior TLI del cual hacemos fork */
} xl_end_of_recovery;
```

---

16 → 15

**ginxlogSplit**

16: mismo para `gin_xlog_update_meta`

```c
typedef struct ginxlogSplit
{
	RelFileLocator locator;
	BlockNumber rrlink;			/* right link, o root's blocknumber si root
								 * split */
	BlockNumber leftChildBlkno; /* válido en un non-leaf split */
	BlockNumber rightChildBlkno;
	uint16		flags;			/* ver debajo */
} ginxlogSplit;
```

15:

```c
typedef struct ginxlogSplit
{
	RelFileNode node;
	BlockNumber rrlink;			/* right link, o root's blocknumber si root
								 * split */
	BlockNumber leftChildBlkno; /* válido en un non-leaf split */
	BlockNumber rightChildBlkno;
	uint16		flags;			/* ver debajo */
} ginxlogSplit;
```

**xl_hash_vacuum_one_page**

16:

```c
typedef struct xl_hash_vacuum_one_page
{
	TransactionId snapshotConflictHorizon;
	uint16		ntuples;
	bool		isCatalogRel;	/* para manejar recovery conflict durante logical
								 * decoding en standby */

	/* TARGET OFFSET NUMBERS */
	OffsetNumber offsets[FLEXIBLE_ARRAY_MEMBER];
} xl_hash_vacuum_one_page;
#define SizeOfHashVacuumOnePage offsetof(xl_hash_vacuum_one_page, offsets)
```

15:

```c
typedef struct xl_hash_vacuum_one_page
{
	TransactionId latestRemovedXid;
	int			ntuples;

	/* TARGET OFFSET NUMBERS FOLLOW AT THE END */
} xl_hash_vacuum_one_page;
#define SizeOfHashVacuumOnePage \
	(offsetof(xl_hash_vacuum_one_page, ntuples) + sizeof(int))
```

**xl_heap_prune**

16:

```c
typedef struct xl_heap_prune
{
	TransactionId snapshotConflictHorizon;
	uint16		nredirected;
	uint16		ndead;
	bool		isCatalogRel;	/* para manejar recovery conflict durante logical
								 * decoding en standby */
	/* OFFSET NUMBERS están en la block reference 0 */
} xl_heap_prune;
#define SizeOfHeapPrune (offsetof(xl_heap_prune, isCatalogRel) + sizeof(bool))
```

15:

```c
typedef struct xl_heap_prune
{
	TransactionId latestRemovedXid;
	uint16		nredirected;
	uint16		ndead;
	/* OFFSET NUMBERS están en la block reference 0 */
} xl_heap_prune;
#define SizeOfHeapPrune (offsetof(xl_heap_prune, ndead) + sizeof(uint16))
```

**xl_heap_freeze_plan**

16:

```c
typedef struct xl_heap_freeze_plan
{
	TransactionId xmax;
	uint16		t_infomask2;
	uint16		t_infomask;
	uint8		frzflags;

	/* Longitud del individual page offset numbers array para este plan */
	uint16		ntuples;
} xl_heap_freeze_plan;
```

15:

```c
typedef struct xl_heap_freeze_tuple
{
	TransactionId xmax;
	OffsetNumber offset;
	uint16		t_infomask2;
	uint16		t_infomask;
	uint8		frzflags;
} xl_heap_freeze_tuple;
```

**xl_heap_freeze_page**

16:

```c
typedef struct xl_heap_freeze_page
{
	TransactionId snapshotConflictHorizon;
	uint16		nplans;
	bool		isCatalogRel;	/* para manejar recovery conflict durante logical
								 * decoding en standby */

	/*
	 * En payload de blk 0 : FREEZE PLANS y OFFSET NUMBER ARRAY
	 */
} xl_heap_freeze_page;
```

15:

```c
typedef struct xl_heap_freeze_page
{
	TransactionId cutoff_xid;
	uint16		ntuples;
} xl_heap_freeze_page;
```

**xl_btree_reuse_page**

16:

```c
typedef struct xl_btree_reuse_page
{
	RelFileLocator locator;
	BlockNumber block;
	FullTransactionId snapshotConflictHorizon;
	bool		isCatalogRel;	/* para manejar recovery conflict durante logical
								 * decoding en standby */
} xl_btree_reuse_page;
```

15:

```c
typedef struct xl_btree_reuse_page
{
	RelFileNode node;
	BlockNumber block;
	FullTransactionId latestRemovedFullXid;
} xl_btree_reuse_page;
```

**xl_btree_delete**

16:

```c
typedef struct xl_btree_delete
{
	TransactionId snapshotConflictHorizon;
	uint16		ndeleted;
	uint16		nupdated;
	bool		isCatalogRel;	/* para manejar recovery conflict durante logical
								 * decoding en standby */

	/*----
	 * En payload de blk 0 :
	 * - DELETED TARGET OFFSET NUMBERS
	 * - UPDATED TARGET OFFSET NUMBERS
	 * - UPDATED TUPLES METADATA (xl_btree_update) ARRAY
	 *----
	 */
} xl_btree_delete;
```

15:

```c
typedef struct xl_btree_delete
{
	TransactionId latestRemovedXid;
	uint16		ndeleted;
	uint16		nupdated;

	/* DELETED TARGET OFFSET NUMBERS FOLLOW */
	/* UPDATED TARGET OFFSET NUMBERS FOLLOW */
	/* UPDATED TUPLES METADATA (xl_btree_update) ARRAY FOLLOWS */
} xl_btree_delete;
```

**spgxlogVacuumRedirect**

16:

```c
typedef struct spgxlogVacuumRedirect
{
	uint16		nToPlaceholder; /* número de redirects para hacer placeholders */
	OffsetNumber firstPlaceholder;	/* primer placeholder tuple a remover */
	TransactionId snapshotConflictHorizon;	/* más nuevo XID de removed redirects */
	bool		isCatalogRel;	/* para manejar recovery conflict durante logical
								 * decoding en standby */

	/* offsets de redirect tuples para hacer placeholders sigue */
	OffsetNumber offsets[FLEXIBLE_ARRAY_MEMBER];
} spgxlogVacuumRedirect;
```

15:

```c
typedef struct spgxlogVacuumRedirect
{
	uint16		nToPlaceholder; /* número de redirects para hacer placeholders */
	OffsetNumber firstPlaceholder;	/* primer placeholder tuple a remover */
	TransactionId newestRedirectXid;	/* más nuevo XID de removed redirects */

	/* offsets de redirect tuples para hacer placeholders sigue */
	OffsetNumber offsets[FLEXIBLE_ARRAY_MEMBER];
} spgxlogVacuumRedirect;
```

---

15 → 14

**xl_xact_prepare**

15:

```c
typedef struct xl_xact_prepare
{
	uint32		magic;			/* format identifier */
	uint32		total_len;		/* actual file length */
	TransactionId xid;			/* original transaction XID */
	Oid			database;		/* OID of database it was in */
	TimestampTz prepared_at;	/* time of preparation */
	Oid			owner;			/* user running the transaction */
	int32		nsubxacts;		/* number of following subxact XIDs */
	int32		ncommitrels;	/* number of delete-on-commit rels */
	int32		nabortrels;		/* number of delete-on-abort rels */
	int32		ncommitstats;	/* number of stats to drop on commit */
	int32		nabortstats;	/* number of stats to drop on abort */
	int32		ninvalmsgs;		/* number of cache invalidation messages */
	bool		initfileinval;	/* does relcache init file need invalidation? */
	uint16		gidlen;			/* length of the GID - GID follows the header */
	XLogRecPtr	origin_lsn;		/* lsn of this record at origin node */
	TimestampTz origin_timestamp;	/* time of prepare at origin node */
} xl_xact_prepare;
```

14:

```c
typedef struct xl_xact_prepare
{
	uint32		magic;			/* format identifier */
	uint32		total_len;		/* actual file length */
	TransactionId xid;			/* original transaction XID */
	Oid			database;		/* OID of database it was in */
	TimestampTz prepared_at;	/* time of preparation */
	Oid			owner;			/* user running the transaction */
	int32		nsubxacts;		/* number of following subxact XIDs */
	int32		ncommitrels;	/* number of delete-on-commit rels */
	int32		nabortrels;		/* number of delete-on-abort rels */
	int32		ninvalmsgs;		/* number of cache invalidation messages */
	bool		initfileinval;	/* does relcache init file need invalidation? */
	uint16		gidlen;			/* length of the GID - GID follows the header */
	XLogRecPtr	origin_lsn;		/* lsn of this record at origin node */
	TimestampTz origin_timestamp;	/* time of prepare at origin node */
} xl_xact_prepare;
```

**xl_xact_parsed_commit**

15:

```c
typedef struct xl_xact_parsed_commit
{
	TimestampTz xact_time;
	uint32		xinfo;

	Oid			dbId;			/* MyDatabaseId */
	Oid			tsId;			/* MyDatabaseTableSpace */

	int			nsubxacts;
	TransactionId *subxacts;

	int			nrels;
	RelFileNode *xnodes;

	int			nstats;
	xl_xact_stats_item *stats;

	int			nmsgs;
	SharedInvalidationMessage *msgs;

	TransactionId twophase_xid; /* only for 2PC */
	char		twophase_gid[GIDSIZE];	/* only for 2PC */
	int			nabortrels;		/* only for 2PC */
	RelFileNode *abortnodes;	/* only for 2PC */
	int			nabortstats;		/* only for 2PC */
	xl_xact_stats_item *abortstats; /* only for 2PC */

	XLogRecPtr	origin_lsn;
	TimestampTz origin_timestamp;
} xl_xact_parsed_commit;
```

14:

```c
typedef struct xl_xact_parsed_commit
{
	TimestampTz xact_time;
	uint32		xinfo;

	Oid			dbId;			/* MyDatabaseId */
	Oid			tsId;			/* MyDatabaseTableSpace */

	int			nsubxacts;
	TransactionId *subxacts;

	int			nrels;
	RelFileNode *xnodes;

	int			nmsgs;
	SharedInvalidationMessage *msgs;

	TransactionId twophase_xid; /* only for 2PC */
	char		twophase_gid[GIDSIZE];	/* only for 2PC */
	int			nabortrels;		/* only for 2PC */
	RelFileNode *abortnodes;	/* only for 2PC */

	XLogRecPtr	origin_lsn;
	TimestampTz origin_timestamp;
} xl_xact_parsed_commit;
```

**xl_xact_parsed_abort**

15:

```c
typedef struct xl_xact_parsed_abort
{
	TimestampTz xact_time;
	uint32		xinfo;

	Oid			dbId;			/* MyDatabaseId */
	Oid			tsId;			/* MyDatabaseTableSpace */

	int			nsubxacts;
	TransactionId *subxacts;

	int			nrels;
	RelFileNode *xnodes;

	int			nstats;
	xl_xact_stats_item *stats;

	TransactionId twophase_xid; /* only for 2PC */
	char		twophase_gid[GIDSIZE];	/* only for 2PC */

	XLogRecPtr	origin_lsn;
	TimestampTz origin_timestamp;
} xl_xact_parsed_abort;
```

14:

```c
typedef struct xl_xact_parsed_abort
{
	TimestampTz xact_time;
	uint32		xinfo;

	Oid			dbId;			/* MyDatabaseId */
	Oid			tsId;			/* MyDatabaseTableSpace */

	int			nsubxacts;
	TransactionId *subxacts;

	int			nrels;
	RelFileNode *xnodes;

	TransactionId twophase_xid; /* only for 2PC */
	char		twophase_gid[GIDSIZE];	/* only for 2PC */

	XLogRecPtr	origin_lsn;
	TimestampTz origin_timestamp;
} xl_xact_parsed_abort;
```

**xlogrecord.h flags**

15:

```c
#define BKPIMAGE_APPLY			0x02	/* page image should be restored
										 * during replay */
/* compression methods supported */
#define BKPIMAGE_COMPRESS_PGLZ	0x04
#define BKPIMAGE_COMPRESS_LZ4	0x08
#define BKPIMAGE_COMPRESS_ZSTD	0x10

#define	BKPIMAGE_COMPRESSED(info) \
	((info & (BKPIMAGE_COMPRESS_PGLZ | BKPIMAGE_COMPRESS_LZ4 | \
			  BKPIMAGE_COMPRESS_ZSTD)) != 0)

```

14:

```c
#define BKPIMAGE_IS_COMPRESSED		0x02	/* page image is compressed */
#define BKPIMAGE_APPLY		0x04	/* page image should be restored during
									 * replay */
```

---

14 → 13

**xl_heap_prune**

14:

```c
typedef struct xl_heap_prune
{
	TransactionId latestRemovedXid;
	uint16		nredirected;
	uint16		ndead;
	/* OFFSET NUMBERS están en la block reference 0 */
} xl_heap_prune;
```

13:

```c
typedef struct xl_heap_clean
{
	TransactionId latestRemovedXid;
	uint16		nredirected;
	uint16		ndead;
	/* OFFSET NUMBERS están en la block reference 0 */
} xl_heap_clean;
```

**xl_heap_vacuum**

14:

```c
typedef struct xl_heap_vacuum
{
	uint16		nunused;
	/* OFFSET NUMBERS están en la block reference 0 */
} xl_heap_vacuum;
```

13:

```c
typedef struct xl_heap_cleanup_info
{
	RelFileNode node;
	TransactionId latestRemovedXid;
} xl_heap_cleanup_info;
```

**xl_btree_metadata**

14:

```c
typedef struct xl_btree_metadata
{
	uint32		version;
	BlockNumber root;
	uint32		level;
	BlockNumber fastroot;
	uint32		fastlevel;
	uint32		last_cleanup_num_delpages;
	bool		allequalimage;
} xl_btree_metadata;
```

13:

```c
typedef struct xl_btree_metadata
{
	uint32		version;
	BlockNumber root;
	uint32		level;
	BlockNumber fastroot;
	uint32		fastlevel;
	TransactionId oldest_btpo_xact;
	float8		last_cleanup_num_heap_tuples;
	bool		allequalimage;
} xl_btree_metadata;
```

**xl_btree_reuse_page**

14:

```c
typedef struct xl_btree_reuse_page
{
	RelFileNode node;
	BlockNumber block;
	FullTransactionId latestRemovedFullXid;
} xl_btree_reuse_page;
```

13:

```c
typedef struct xl_btree_reuse_page
{
	RelFileNode node;
	BlockNumber block;
	TransactionId latestRemovedXid;
} xl_btree_reuse_page;
```

**xl_btree_delete**

14:

```c
typedef struct xl_btree_delete
{
	TransactionId latestRemovedXid;
	uint32		ndeleted;

	/* DELETED TARGET OFFSET NUMBERS FOLLOW */
} xl_btree_delete;
```

13:

```c
typedef struct xl_btree_delete
{
	TransactionId latestRemovedXid;
	uint32		ndeleted;

	/* DELETED TARGET OFFSET NUMBERS FOLLOW */
} xl_btree_delete;
```

**xl_btree_unlink_page**

14:

```c
typedef struct xl_btree_unlink_page
{
	BlockNumber leftsib;		/* left sibling de target block, si alguno */
	BlockNumber rightsib;		/* right sibling de target block */
	uint32		level;			/* level de target block */
	FullTransactionId safexid;	/* BTPageSetDeleted() XID de target block */

	/*
	 * Información needed para recrear una half-dead leaf page con correct
	 * topparent link. Los campos solo son usados cuando el target page de
	 * deletion operation es un internal page. REDO routine crea half-dead page
	 * from scratch para mantener things simple (este es el mismo convenient
	 * approach usado para el target page en sí).
	 */
	BlockNumber leafleftsib;
	BlockNumber leafrightsib;
	BlockNumber leaftopparent;	/* next child down en el subtree */

	/* xl_btree_metadata FOLLOWS IF XLOG_BTREE_UNLINK_PAGE_META */
} xl_btree_unlink_page;
```

13:

```c
typedef struct xl_btree_unlink_page
{
	BlockNumber leftsib;		/* left sibling de target block, si alguno */
	BlockNumber rightsib;		/* right sibling de target block */

	/*
	 * Información needed para recrear la leaf page, cuando target es un
	 * internal page.
	 */
	BlockNumber leafleftsib;
	BlockNumber leafrightsib;
	BlockNumber topparent;		/* next child down en la branch */

	TransactionId btpo_xact;	/* value de btpo.xact para usar en recovery */
	/* xl_btree_metadata FOLLOWS IF XLOG_BTREE_UNLINK_PAGE_META */
} xl_btree_unlink_page;
```

### Información adicional

Para más detalles sobre el funcionamiento interno y funciones helper adicionales usadas en `parse_wal_file`, consulta el código fuente en `wal_reader.c`.

Esta documento contiene información muy tecnica, lo que dificulta la traducción, en caso de confusion leer la documentación en inglés.