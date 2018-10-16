# mmaplloc (C)
==============

malloc &amp; free &amp; realloc &amp; associated functions allowing backing overlay to filesystem mmapped files (in C, which are way, way faster than swapping) PLUS associated tools to help building numerous stores for C++ data structure containers.
If more than one backing overlay files are specifyed, the library do its best to split it in several mmap allocations on contiguous regions (lets say, by 1MiB each), designed to allow different filesystems (and devices) to be used on a pseudo-parallel scheme, for performance improvements. There is a Spike program designed to infer the best block size for each device.

1) a estrutura de cabeçalho do bloco livre (FreeHeader) pode ser:
	FAST_FREE   -- size_t: total_size, size_t: next_free, size_t: prev_free -- 3*size_t
	TIGHT_MEM   -- size_t: total_size, size_t: next_free
	or both. FAST_FREE if the 'total_size' >= 3*size_t, TIGHT_MEM otherwise.
	FreeHeader seria um union do FAST_FREE e TIGHT_MEM;
2) a estrutura de cabeçalho do bloco alocado (AllocHeader) é:
	size_t: total_size

NOTE: all pointers mentioned on this document are relative -- better nominated as 'offset pointers'. Their value is relative to the address of the structure they belong to and can be positive or negative.

3) O mínimo espaço alocado terá tamanho total igual ao menor cabeçalho do bloco livre (2*size_t). Se a biblioteca for compilada com a opção de debug, um aviso será exibido em 'stderr' sempre que uma alocação menor ocorrer, afim de alertar para o desperdício;
4) É possível pedir a alocação de n página(s), onde o bloco será retornado alinhado com as páginas da máquina e não será gasto nenhum byte com overhead -- uma sequencia de palloc(n) retornará ponteiros contíguos. free, neste caso, inserirá os blocos em sua estrutura


Módulo de páginas (C++):
=======================

1) vector de inteiros terá o primeiro inteiro da primeira página reservado para armazenar o length dele. vector[0] aponta para base+sizeof(size_t) e os próximos (4096/sizeof(size_t))-2 elementos da primeira página estão disponíveis. O último elemento será o ponteiro (na verdade offset) para uma eventual próxima página. Pŕoximas páginas terão os primeiros (4096/sizeof(size_t))-1 úteis, sendo o último também reservado para uma eventual próxima página;
2) um vector de strings ou de tipos com tamanho variável (inclusive outros vectors) será representado como um vector de inteiros, onde cada elemento é um offset pointer para o ítem real;
3) vectors de outros tamanhos (byte, short, long, float, double, struct), igualmente, terão o primeiro size_t da primeira página pra guardar o length e o último size_t para guardar o ponteiro offset para a próxima página;
4) pode-se usar malloc para alocar itens de tamanhos variáveis, porém haverá o overhead de um size_t, podendo chegar a 8 bytes. Uma outra alternativa seria criar listas contíguas. Por exemplo:
a) a unidade básica de alocação para elementos de tamanho variável consiste num número n de paginas sequenciais (alocadas com palloc(n)). O primeiro size_t da primeira página terá o número de páginas da sequencia e o último size_t da última página terá um ponteiro offset para uma eventual próxima sequência. Dos bytes úteis de cada sequencia, o primeiro inteiro size_t contém o offset para a primeira posição livre, ainda não alocada (em oposição a posições deletadas, que serão abordadas mais tarde), seja nesta sequencia de páginas ou em qualquer outra. Os bytes restantes conterão os elementos, dispostos da seguinte maneira: um inteiro (int8, int16, int32 ou int64) contém o comprimento do elemento, logo após, os bytes do elemento. O ponteiro para a proxima posição não alocada será atualizado... e assim por diante. Faltou ainda registrarmos que os bytes para o tamanho de cada elemento serão 8, 16, 32 ou 64 bits. Isso se dará de forma automática. É possível que o primeiro size_t da primeira página contenha o ponteiro offset para a primeira posição ainda não alocada e que o inteiro seguinte represente o numero de paginas da sequencia.
b) para strings ou arrays de bytes: cada página (?é, inicialmente, inicializada com zeros?) guarda, nos primeiros sizeof(size_t) bytes um ponteiro offset para a primeira sequencia de bytes não utilizada.


Shrinking:
=========

1) When an element (on a variable sized page set) is deleted, it's space must be filled with zeroes (including the integer with the size of the element), but the next_uninserted_position pointer is not touched;
2) If one passes a vector of pointers to variable sized elements, the algorithm might rearrange gaps on the structure in order to save space. The rearrangement can be:
    - Scatered page sets might be able to get united, in the case of some adjacent pages got freed -- the last bytes on every page set are likely to waste half of an average sized element
    - Space from deleted elements can be reused by moving back the next elements and rewriting their offset pointers
    - This operation should block writers but not readers. Corruption should not occur in the case the process is killed (but not if the machine crashes -- fsync is not necessary)
3) Removing an element from a fixed sized vector (or from a vector of pointers to a variable sized elements page set) implies rewriting back all elements above the desired position. We should also support to delete a range of elements. This operation should block writers and readers as well, since the traversal of elements at the same time of a deletion might return the same element(s) twice or skip some element(s), depending on the order the traversal is made.
