/*
* Copyright (c) 1999-2002  Shane Hudson
* Copyright (c) 2006-2009  Pascal Georges
* Copyright (C) 2014  Fulvio Benini

* This file is part of Scid (Shane's Chess Information Database).
*
* Scid is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation.
*
* Scid is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Scid.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SCID_INDEX_H
#define SCID_INDEX_H

#include "common.h"
#include "date.h"
#include "indexentry.h"
#include "filter.h"
#include "filebuf.h"
#include <string>
#include <vector>

class NameBase;
class SortCache;

//////////////////////////////////////////////////////////////////////
//  Index:  Constants

const char         INDEX_SUFFIX[]     = ".si4";
const char         OLD_INDEX_SUFFIX[] = ".si3";
const char         INDEX_MAGIC[8]     = "Scid.si";
// max. number of games is 2^(3*8)-1-1,
// The "2^(3*8)-1" as si4 only uses three bytes to store this integer,
// The second "-1" because GetAutoLoad uses 0 to mean "no autoload"
const gamenumT     MAX_GAMES          = 16777214;

// Descriptions can be up to 107 bytes long.
const uint  SCID_DESC_LENGTH = 107;
const uint  CUSTOM_FLAG_DESC_LENGTH = 8;
const uint  CUSTOM_FLAG_MAX = 6;

// Header on-disk size: magic=8, version=2, numGames=3, baseType=4, autoLoad=3
// Description length = 111 bytes including trailing '\0'.
// Custom flag desc length = 9 bytes including trailing '\0'.
// So total is 128 bytes + 9*6 = 182 bytes for the whole header.
const uint  INDEX_HEADER_SIZE = 8 + 2 + 3 + 4 + 3 + SCID_DESC_LENGTH + 1 + (CUSTOM_FLAG_DESC_LENGTH+1) * CUSTOM_FLAG_MAX;
const uint  OLD_INDEX_HEADER_SIZE = INDEX_HEADER_SIZE - (CUSTOM_FLAG_DESC_LENGTH+1) * CUSTOM_FLAG_MAX;

// INDEX_MaxSortCriteria is the maximum number of fields allowed in
// a sorting criteria list.
const uint SORTING_CACHE_MAX = 8;
const uint INDEX_MaxSortingCriteria = 16;
const uint IDX_NOT_FOUND = 0xffffffff;


//////////////////////////////////////////////////////////////////////
//  Index:  Class Definition

class Index
{
private:
    struct { // one at the start of the index file.
        char        magic[9];    // 8-byte identifier for Scid index files.
        versionT    version;     // version number. 2 bytes.
        uint        baseType;    // Type, e.g. tournament, theory, etc.
        gamenumT    numGames;    // number of games in file.
        gamenumT    autoLoad;    // game number to autoload: 0=none, 1=1st, >numGames=last
        // description is a fixed-length string describing the database.
        char        description [SCID_DESC_LENGTH + 1];
        // short description (8 chars) for the CUSTOM_FLAG_MAX bits for CUSTOM flags
        char        customFlagDesc [CUSTOM_FLAG_MAX][CUSTOM_FLAG_DESC_LENGTH+1] ;
    } Header;
    bool         Dirty;         // If true, Header needs rewriting to disk.

    Filebuf*     FilePtr;       // filehandle for opened index file.
    fileModeT    fileMode_;     // Mode: e.g. FILE_WRITEONLY

    mutable SortCache* sortingCaches[SORTING_CACHE_MAX];
    bool filter_changed_;
    int badNameIdCount_;
    uint sequentialWrite_;

    // The complete index will be loaded in memory and can be pretty huge.
    // To avoid the slow reallocation when adding games we split the data in chunks.
    // CHUNKSHIFT is the base-2 logarithm of the number of index entries allocated as one chunk.
    // i.e 16 = 2^16 = 65536 (total size of one chunk: 65536*48 = 3MB)
    VectorBig<IndexEntry, 16> entries_; // A two-level array of the entire index.

    Index(const Index&);
    Index& operator=(const Index&);
    void Init ();
    errorT Clear ();
    errorT write (const IndexEntry* ie, gamenumT idx);

public:
    Index()  { entries_.reserve(MAX_GAMES); Init(); }
    ~Index() { Clear(); }

    errorT Open(const char* filename, fileModeT fmode = FMODE_Both);
    errorT Create(const char* filename);
    errorT Close() { return Clear(); }

    gamenumT GetNumGames () const { return Header.numGames; }
    int GetBadNameIdCount() const { return badNameIdCount_; }

    errorT ReadEntireFile (NameBase* nb, const Progress& progress);

    IndexEntry* FetchEntry (gamenumT g) { return &(entries_[g]); }
    const IndexEntry* GetEntry (gamenumT g) const { return &(entries_[g]); }

    uint        GetType () const { return Header.baseType; }
    versionT    GetVersion () const { return Header.version; }
    const char* GetDescription () const { return Header.description; }
    const char* GetCustomFlagDesc (byte c) const {
        if (c < IDX_FLAG_CUSTOM1 || c > IDX_FLAG_CUSTOM6) return 0;
        return Header.customFlagDesc[c - IDX_FLAG_CUSTOM1];
    }
    gamenumT GetAutoLoad () const {
        return (Header.autoLoad <= Header.numGames) ? Header.autoLoad : Header.numGames;
    }

    // Functions that modify the Header
    // For performance reasons changes are not immediately written to index file.
    // Changes are automatically written to file when the object is destroyed or closed.
    // However, for maximum security against power loss, crash, etc,
    // manually call the function WriteHeader()
    void SetType (uint t) {
        Header.baseType = t;
        Dirty = true;
    }
    
    void SetVersion (versionT v) {
        Header.version = v;
        Dirty = true;
    }
    
    void SetDescription (const char* str) {
        strncpy(Header.description, str, SCID_DESC_LENGTH);
        Header.description[SCID_DESC_LENGTH] = 0;
        Dirty = true;
    }
    
    void SetCustomFlagDesc (byte c, const char* str) {
        if (c < IDX_FLAG_CUSTOM1 || c > IDX_FLAG_CUSTOM6) return;
        char* flagDesc = Header.customFlagDesc[c - IDX_FLAG_CUSTOM1];
        strncpy(flagDesc, str, CUSTOM_FLAG_DESC_LENGTH);
        flagDesc[CUSTOM_FLAG_DESC_LENGTH] = 0;
        Dirty = true;
    }
    void SetAutoLoad (gamenumT gnum) {
        Header.autoLoad = gnum;
        Dirty = true;
    }

    errorT WriteHeader ();
    errorT WriteEntry (const IndexEntry* ie, gamenumT idx, bool flush = true) {
        errorT res = write(ie, idx);
        if (flush && res == OK && FilePtr != NULL) {
            res = (FilePtr->pubsync() != -1) ? OK : ERROR_FileWrite;;
        }
        return res;
    }
    errorT AddGame (const IndexEntry* ie) {
        return WriteEntry(ie, GetNumGames(), false);
    }

    /* CreateSortingCache
     * Create a SortCache that requires 4 byte for each game
     * A SortCache will greatly speedup the GetRange* functions that use the same criteria
     * nbase:    pointer to the NameBase where to get data from.
     * criteria: string that identify the sort order, composed by a letter (see sortcache.cpp for a list of valid values)
     *           and + (ascending) or - (descending).
     *           For example: "i-d-" means sort by rating desc, date desc
     * Return:   a pointer to the newly created SortCache
     *           0 in case of error
     */
    SortCache* CreateSortCache (const NameBase* nbase, const char* criteria) const;

    /* FreeCache
     * Release the memory of a SortCache (previously created by CreateSortingCache)
     * criteria: string that identify the sort order
     */
    void FreeSortCache(const char* criteria) const;

    /* GetRange
     * Get a list of ordered game indexes sorted by criteria
     * nbase:    pointer to the NameBase where to get data from.
     * criteria: string that identify the sort order
     * idx:      get result after position idx (0 to get the first games)
     * count:    number of results wanted
     * filter:   include only the game where filter->Get() > 0
     * result:   pointer to an array of uint where the sorted list will be stored
     *           *result* array must have size >= count
     *           if there aren't enough result to reach count: result[last_result + 1] = IDX_NOT_FOUND
     */
    errorT GetRange(const NameBase *nbase, const char *criteria, uint idx, uint count, const HFilter& filter, uint *result) const;

    /* GetRangeLocation
     * Find the location of a game into a sorted Range.
     * The result of the functions and param gnumber are index by base 1.
     * I.E. if GetRangeLocation(... gnumber=10 ...) = 40 then GetRange (... idx=39 ...) will return result[0] = 9;
     * gnumber: index of the game to search
     * text:    search any game that contains *text* into white name or black name or site name or event name (case insensitive)
     * start:   start the search after position *start* if forward =true, else search games before *start*
     * Return:  the position of the searched game (first game = 1)
     *          0 if not found
     */
    uint GetRangeLocation (const NameBase *nbase, const char *criteria, const HFilter& filter, uint gnumber) const;
    uint GetRangeLocation (const NameBase *nbase, const char *criteria, const HFilter& filter,
                           const char* text, uint start, bool forward) const;
    /* IndexUpdated
     * When changes are made to the games (adding or saving a game) the sortcaches need to be updated.
     * if gnum == IDX_NOT_FOUND the sortcache will be completely rebuild (faster for a large number of updates)
     */
    errorT IndexUpdated(uint gnum) const;
};


#endif  // #ifdef SCID_INDEX_H

//////////////////////////////////////////////////////////////////////
//  EOF: index.h
//////////////////////////////////////////////////////////////////////

