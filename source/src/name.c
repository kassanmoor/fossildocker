/*
** Copyright (c) 2006 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code used to convert user-supplied object names into
** canonical UUIDs.
**
** A user-supplied object name is any unique prefix of a valid UUID but
** not necessarily in canonical form.
*/
#include "config.h"
#include "name.h"
#include <assert.h>

/*
** Return TRUE if the string begins with something that looks roughly
** like an ISO date/time string.  The SQLite date/time functions will
** have the final say-so about whether or not the date/time string is
** well-formed.
*/
int fossil_isdate(const char *z){
  if( !fossil_isdigit(z[0]) ) return 0;
  if( !fossil_isdigit(z[1]) ) return 0;
  if( !fossil_isdigit(z[2]) ) return 0;
  if( !fossil_isdigit(z[3]) ) return 0;
  if( z[4]!='-') return 0;
  if( !fossil_isdigit(z[5]) ) return 0;
  if( !fossil_isdigit(z[6]) ) return 0;
  if( z[7]!='-') return 0;
  if( !fossil_isdigit(z[8]) ) return 0;
  if( !fossil_isdigit(z[9]) ) return 0;
  return 1;
}

/*
** Convert a symbolic name into a RID.  Acceptable forms:
**
**   *  SHA1 hash
**   *  SHA1 hash prefix of at least 4 characters
**   *  Symbolic Name
**   *  "tag:" + symbolic name
**   *  Date or date-time
**   *  "date:" + Date or date-time
**   *  symbolic-name ":" date-time
**   *  "tip"
**
** The following additional forms are available in local checkouts:
**
**   *  "current"
**   *  "prev" or "previous"
**   *  "next"
**
** Return the RID of the matching artifact.  Or return 0 if the name does not
** match any known object.  Or return -1 if the name is ambiguous.
**
** The zType parameter specifies the type of artifact: ci, t, w, e, g.
** If zType is NULL or "" or "*" then any type of artifact will serve.
** zType is "ci" in most use cases since we are usually searching for
** a check-in.
*/
int symbolic_name_to_rid(const char *zTag, const char *zType){
  int vid;
  int rid = 0;
  int nTag;
  int i;

  if( zType==0 || zType[0]==0 ) zType = "*";
  if( zTag==0 || zTag[0]==0 ) return 0;

  /* special keyword: "tip" */
  if( fossil_strcmp(zTag, "tip")==0 && (zType[0]=='*' || zType[0]=='c') ){
    rid = db_int(0,
      "SELECT objid"
      "  FROM event"
      " WHERE type='ci'"
      " ORDER BY event.mtime DESC"
    );
    if( rid ) return rid;
  }

  /* special keywords: "prev", "previous", "current", and "next" */
  if( g.localOpen && (vid=db_lget_int("checkout",0))!=0 ){
    if( fossil_strcmp(zTag, "current")==0 ){
      rid = vid;
    }else if( fossil_strcmp(zTag, "prev")==0
              || fossil_strcmp(zTag, "previous")==0 ){
      rid = db_int(0, "SELECT pid FROM plink WHERE cid=%d AND isprim", vid);
    }else if( fossil_strcmp(zTag, "next")==0 ){
      rid = db_int(0, "SELECT cid FROM plink WHERE pid=%d"
                      "  ORDER BY isprim DESC, mtime DESC", vid);
    }
    if( rid ) return rid;
  }

  /* Date and times */
  if( memcmp(zTag, "date:", 5)==0 ){
    rid = db_int(0,
      "SELECT objid FROM event"
      " WHERE mtime<=julianday(%Q,'utc') AND type GLOB '%q'"
      " ORDER BY mtime DESC LIMIT 1",
      &zTag[5], zType);
    return rid;
  }
  if( fossil_isdate(zTag) ){
    rid = db_int(0,
      "SELECT objid FROM event"
      " WHERE mtime<=julianday(%Q,'utc') AND type GLOB '%q'"
      " ORDER BY mtime DESC LIMIT 1",
      zTag, zType);
    if( rid) return rid;
  }

  /* Deprecated date & time formats:   "local:" + date-time and
  ** "utc:" + date-time */
  if( memcmp(zTag, "local:", 6)==0 ){
    rid = db_int(0,
      "SELECT objid FROM event"
      " WHERE mtime<=julianday(%Q) AND type GLOB '%q'"
      " ORDER BY mtime DESC LIMIT 1",
      &zTag[6], zType);
    return rid;
  }
  if( memcmp(zTag, "utc:", 4)==0 ){
    rid = db_int(0,
      "SELECT objid FROM event"
      " WHERE mtime<=julianday('%qz') AND type GLOB '%q'"
      " ORDER BY mtime DESC LIMIT 1",
      &zTag[4], zType);
    return rid;
  }

  /* "tag:" + symbolic-name */
  if( memcmp(zTag, "tag:", 4)==0 ){
    rid = db_int(0,
       "SELECT event.objid, max(event.mtime)"
       "  FROM tag, tagxref, event"
       " WHERE tag.tagname='sym-%q' "
       "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype>0 "
       "   AND event.objid=tagxref.rid "
       "   AND event.type GLOB '%q'",
       &zTag[4], zType
    );
    return rid;
  }

  /* root:TAG -> The origin of the branch */
  if( memcmp(zTag, "root:", 5)==0 ){
    Stmt q;
    int rc;
    char *zBr;
    rid = symbolic_name_to_rid(zTag+5, zType);
    zBr = db_text("trunk","SELECT value FROM tagxref"
                          " WHERE rid=%d AND tagid=%d"
                          " AND tagtype>0",
                          rid, TAG_BRANCH);
    db_prepare(&q,
      "SELECT pid, EXISTS(SELECT 1 FROM tagxref"
                         " WHERE tagid=%d AND tagtype>0"
                         "   AND value=%Q AND rid=plink.pid)"
      "  FROM plink"
      " WHERE cid=:cid AND isprim",
      TAG_BRANCH, zBr
    );
    fossil_free(zBr);
    do{
      db_reset(&q);
      db_bind_int(&q, ":cid", rid);
      rc = db_step(&q);
      if( rc!=SQLITE_ROW ) break;
      rid = db_column_int(&q, 0);
    }while( db_column_int(&q, 1)==1 && rid>0 );
    db_finalize(&q);
    return rid;
  }

  /* symbolic-name ":" date-time */
  nTag = strlen(zTag);
  for(i=0; i<nTag-10 && zTag[i]!=':'; i++){}
  if( zTag[i]==':' && fossil_isdate(&zTag[i+1]) ){
    char *zDate = mprintf("%s", &zTag[i+1]);
    char *zTagBase = mprintf("%.*s", i, zTag);
    int nDate = strlen(zDate);
    if( sqlite3_strnicmp(&zDate[nDate-3],"utc",3)==0 ){
      zDate[nDate-3] = 'z';
      zDate[nDate-2] = 0;
    }
    rid = db_int(0,
      "SELECT event.objid, max(event.mtime)"
      "  FROM tag, tagxref, event"
      " WHERE tag.tagname='sym-%q' "
      "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype>0 "
      "   AND event.objid=tagxref.rid "
      "   AND event.mtime<=julianday(%Q)"
      "   AND event.type GLOB '%q'",
      zTagBase, zDate, zType
    );
    return rid;
  }

  /* SHA1 hash or prefix */
  if( nTag>=4 && nTag<=UUID_SIZE && validate16(zTag, nTag) ){
    Stmt q;
    char zUuid[UUID_SIZE+1];
    memcpy(zUuid, zTag, nTag+1);
    canonical16(zUuid, nTag);
    rid = 0;
    if( zType[0]=='*' ){
      db_prepare(&q, "SELECT rid FROM blob WHERE uuid GLOB '%s*'", zUuid);
    }else{
      db_prepare(&q,
        "SELECT blob.rid"
        "  FROM blob, event"
        " WHERE blob.uuid GLOB '%s*'"
        "   AND event.objid=blob.rid"
        "   AND event.type GLOB '%q'",
        zUuid, zType
      );
    }
    if( db_step(&q)==SQLITE_ROW ){
      rid = db_column_int(&q, 0);
      if( db_step(&q)==SQLITE_ROW ) rid = -1;
    }
    db_finalize(&q);
    if( rid ) return rid;
  }

  /* Symbolic name */
  rid = db_int(0,
    "SELECT event.objid, max(event.mtime)"
    "  FROM tag, tagxref, event"
    " WHERE tag.tagname='sym-%q' "
    "   AND tagxref.tagid=tag.tagid AND tagxref.tagtype>0 "
    "   AND event.objid=tagxref.rid "
    "   AND event.type GLOB '%q'",
    zTag, zType
  );
  if( rid>0 ) return rid;

  /* Undocumented:  numeric tags get translated directly into the RID */
  if( memcmp(zTag, "rid:", 4)==0 ){
    zTag += 4;
    for(i=0; fossil_isdigit(zTag[i]); i++){}
    if( zTag[i]==0 ){
      if( strcmp(zType,"*")==0 ){
        rid = atoi(zTag);
      }else{
        rid = db_int(0,
          "SELECT event.objid"
          "  FROM event"
          " WHERE event.objid=%s"
          "   AND event.type GLOB '%q'", zTag, zType);
      }
    }
  }
  return rid;
}

/*
** This routine takes a user-entered UUID which might be in mixed
** case and might only be a prefix of the full UUID and converts it
** into the full-length UUID in canonical form.
**
** If the input is not a UUID or a UUID prefix, then try to resolve
** the name as a tag.  If multiple tags match, pick the latest.
** If the input name matches "tag:*" then always resolve as a tag.
**
** If the input is not a tag, then try to match it as an ISO-8601 date
** string YYYY-MM-DD HH:MM:SS and pick the nearest check-in to that date.
** If the input is of the form "date:*" then always resolve the name as
** a date. The forms "utc:*" and "local:" are deprecated.
**
** Return 0 on success.  Return 1 if the name cannot be resolved.
** Return 2 name is ambiguous.
*/
int name_to_uuid(Blob *pName, int iErrPriority, const char *zType){
  char *zName = blob_str(pName);
  int rid = symbolic_name_to_rid(zName, zType);
  if( rid<0 ){
    fossil_error(iErrPriority, "ambiguous name: %s", zName);
    return 2;
  }else if( rid==0 ){
    fossil_error(iErrPriority, "not found: %s", zName);
    return 1;
  }else{
    blob_reset(pName);
    db_blob(pName, "SELECT uuid FROM blob WHERE rid=%d", rid);
    return 0;
  }
}

/*
** This routine is similar to name_to_uuid() except in the form it
** takes its parameters and returns its value, and in that it does not
** treat errors as fatal. zName must be a UUID, as described for
** name_to_uuid(). zType is also as described for that function. If
** zName does not resolve, 0 is returned. If it is ambiguous, a
** negative value is returned. On success the rid is returned and
** pUuid (if it is not NULL) is set to the a newly-allocated string,
** the full UUID, which must eventually be free()d by the caller.
*/
int name_to_uuid2(char const *zName, const char *zType, char **pUuid){
  int rid = symbolic_name_to_rid(zName, zType);
  if((rid>0) && pUuid){
    *pUuid = db_text(NULL, "SELECT uuid FROM blob WHERE rid=%d", rid);
  }
  return rid;
}


/*
** name_collisions searches through events, blobs, and tickets for
** collisions of a given UUID based on its length on UUIDs no shorter
** than 4 characters in length.
*/
int name_collisions(const char *zName){
  Stmt q;
  int c = 0;         /* count of collisions for zName */
  int nLen;          /* length of zName */
  nLen = strlen(zName);
  if( nLen>=4 && nLen<=UUID_SIZE && validate16(zName, nLen) ){
    db_prepare(&q,
      "SELECT count(uuid) FROM"
      "  (SELECT substr(tkt_uuid, 1, %d) AS uuid FROM ticket"
      "   UNION ALL SELECT * FROM"
      "     (SELECT substr(tagname, 7, %d) FROM"
      "       tag WHERE tagname GLOB 'event-*')"
      "   UNION ALL SELECT * FROM"
      "     (SELECT substr(uuid, 1, %d) FROM blob))"
      "  WHERE uuid GLOB '%q*'"
      "  GROUP BY uuid HAVING count(uuid) > 1;",
      nLen, nLen, nLen, zName);
    if( db_step(&q)==SQLITE_ROW ){
      c = db_column_int(&q, 0);
    }
    db_finalize(&q);
  }
  return c;
}

/*
** COMMAND:  test-name-to-id
**
** Convert a name to a full artifact ID.
*/
void test_name_to_id(void){
  int i;
  Blob name;
  db_must_be_within_tree();
  for(i=2; i<g.argc; i++){
    blob_init(&name, g.argv[i], -1);
    fossil_print("%s -> ", g.argv[i]);
    if( name_to_uuid(&name, 1, "*") ){
      fossil_print("ERROR: %s\n", g.zErrMsg);
      fossil_error_reset();
    }else{
      fossil_print("%s\n", blob_buffer(&name));
    }
    blob_reset(&name);
  }
}

/*
** Convert a name to a rid.  If the name can be any of the various forms
** accepted:
**
**   * SHA1 hash or prefix thereof
**   * symbolic name
**   * date
**   * label:date
**   * prev, previous
**   * next
**   * tip
**
** This routine is used by command-line routines to resolve command-line inputs
** into a rid.
*/
int name_to_typed_rid(const char *zName, const char *zType){
  int rid;

  if( zName==0 || zName[0]==0 ) return 0;
  rid = symbolic_name_to_rid(zName, zType);
  if( rid<0 ){
    fossil_error(1, "ambiguous name: %s", zName);
    return 0;
  }else if( rid==0 ){
    fossil_error(1, "not found: %s", zName);
    return 0;
  }else{
    return rid;
  }
}
int name_to_rid(const char *zName){
  return name_to_typed_rid(zName, "*");
}

/*
** WEBPAGE: ambiguous
** URL: /ambiguous?name=UUID&src=WEBPAGE
**
** The UUID given by the name parameter is ambiguous.  Display a page
** that shows all possible choices and let the user select between them.
*/
void ambiguous_page(void){
  Stmt q;
  const char *zName = P("name");
  const char *zSrc = P("src");
  char *z;

  if( zName==0 || zName[0]==0 || zSrc==0 || zSrc[0]==0 ){
    fossil_redirect_home();
  }
  style_header("Ambiguous Artifact ID");
  @ <p>The artifact id <b>%h(zName)</b> is ambiguous and might
  @ mean any of the following:
  @ <ol>
  z = mprintf("%s", zName);
  canonical16(z, strlen(z));
  db_prepare(&q, "SELECT uuid, rid FROM blob WHERE uuid GLOB '%q*'", z);
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    int rid = db_column_int(&q, 1);
    @ <li><p><a href="%s(g.zTop)/%T(zSrc)/%s(zUuid)">
    @ %s(zUuid)</a> -
    object_description(rid, 0, 0);
    @ </p></li>
  }
  db_finalize(&q);
  db_prepare(&q,
    "   SELECT tkt_rid, tkt_uuid, title"
    "     FROM ticket, ticketchng"
    "    WHERE ticket.tkt_id = ticketchng.tkt_id"
    "      AND tkt_uuid GLOB '%q*'"
    " GROUP BY tkt_uuid"
    " ORDER BY tkt_ctime DESC", z);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0); 
    const char *zUuid = db_column_text(&q, 1);
    const char *zTitle = db_column_text(&q, 2);
    @ <li><p><a href="%s(g.zTop)/%T(zSrc)/%s(zUuid)">
    @ %s(zUuid)</a> -
    @ <ul></ul>
    @ Ticket
    hyperlink_to_uuid(zUuid);
    @ - %s(zTitle).
    @ <ul><li>
    object_description(rid, 0, 0);
    @ </li></ul>
    @ </p></li>
  }
  db_finalize(&q);
  db_prepare(&q,
    "SELECT rid, uuid FROM"
    "  (SELECT tagxref.rid AS rid, substr(tagname, 7) AS uuid"
    "     FROM tagxref, tag WHERE tagxref.tagid = tag.tagid"
    "      AND tagname GLOB 'event-%q*') GROUP BY uuid", z);
  while( db_step(&q)==SQLITE_ROW ){
    int rid = db_column_int(&q, 0); 
    const char* zUuid = db_column_text(&q, 1);
    @ <li><p><a href="%s(g.zTop)/%T(zSrc)/%s(zUuid)">
    @ %s(zUuid)</a> -
    @ <ul><li>
    object_description(rid, 0, 0);
    @ </li></ul>
    @ </p></li>
  }
  @ </ol>
  db_finalize(&q);
  style_footer();
}

/*
** Convert the name in CGI parameter zParamName into a rid and return that
** rid.  If the CGI parameter is missing or is not a valid artifact tag,
** return 0.  If the CGI parameter is ambiguous, redirect to a page that
** shows all possibilities and do not return.
*/
int name_to_rid_www(const char *zParamName){
  int rid;
  const char *zName = P(zParamName);
#ifdef FOSSIL_ENABLE_JSON
  if(!zName && fossil_has_json()){
    zName = json_find_option_cstr(zParamName,NULL,NULL);
  }
#endif
  if( zName==0 || zName[0]==0 ) return 0;
  rid = symbolic_name_to_rid(zName, "*");
  if( rid<0 ){
    cgi_redirectf("%s/ambiguous/%T?src=%t", g.zTop, zName, g.zPath);
    rid = 0;
  }
  return rid;
}

/*
** Generate a description of artifact "rid"
*/
static void whatis_rid(int rid, int verboseFlag){
  Stmt q;
  int cnt;

  /* Basic information about the object. */
  db_prepare(&q,
     "SELECT uuid, size, datetime(mtime%s), ipaddr"
     "  FROM blob, rcvfrom"
     " WHERE rid=%d"
     "   AND rcvfrom.rcvid=blob.rcvid",
     timeline_utc(), rid);
  if( db_step(&q)==SQLITE_ROW ){
    if( verboseFlag ){
      fossil_print("artifact:   %s (%d)\n", db_column_text(&q,0), rid);
      fossil_print("size:       %d bytes\n", db_column_int(&q,1));
      fossil_print("received:   %s from %s\n",
         db_column_text(&q, 2),
         db_column_text(&q, 3));
    }else{
      fossil_print("artifact:   %s\n", db_column_text(&q,0));
      fossil_print("size:       %d bytes\n", db_column_int(&q,1));
    }
  }
  db_finalize(&q);

  /* Report any symbolic tags on this artifact */
  db_prepare(&q,
    "SELECT substr(tagname,5)"
    "  FROM tag JOIN tagxref ON tag.tagid=tagxref.tagid"
    " WHERE tagxref.rid=%d"
    "   AND tagname GLOB 'sym-*'"
    " ORDER BY 1",
    rid
  );
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPrefix = cnt++ ? ", " : "tags:       ";
    fossil_print("%s%s", zPrefix, db_column_text(&q,0));
  }
  if( cnt ) fossil_print("\n");
  db_finalize(&q);

  /* Report any HIDDEN, PRIVATE, CLUSTER, or CLOSED tags on this artifact */
  db_prepare(&q,
    "SELECT tagname"
    "  FROM tag JOIN tagxref ON tag.tagid=tagxref.tagid"
    " WHERE tagxref.rid=%d"
    "   AND tag.tagid IN (5,6,7,9)"
    " ORDER BY 1",
    rid, rid
  );
  cnt = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zPrefix = cnt++ ? ", " : "raw-tags:   ";
    fossil_print("%s%s", zPrefix, db_column_text(&q,0));
  }
  if( cnt ) fossil_print("\n");
  db_finalize(&q);

  /* Check for entries on the timeline that reference this object */
  db_prepare(&q,
     "SELECT type, datetime(mtime%s),"
     "       coalesce(euser,user), coalesce(ecomment,comment)"
     "  FROM event WHERE objid=%d", timeline_utc(), rid);
  if( db_step(&q)==SQLITE_ROW ){
    const char *zType;
    switch( db_column_text(&q,0)[0] ){
      case 'c':  zType = "Check-in";       break;
      case 'w':  zType = "Wiki-edit";      break;
      case 'e':  zType = "Event";          break;
      case 't':  zType = "Ticket-change";  break;
      case 'g':  zType = "Tag-change";     break;
      default:   zType = "Unknown";        break;
    }
    fossil_print("type:       %s by %s on %s\n", zType, db_column_text(&q,2),
                 db_column_text(&q, 1));
    fossil_print("comment:    ");
    comment_print(db_column_text(&q,3), 12, 78);
  }
  db_finalize(&q);

  /* Check to see if this object is used as a file in a check-in */
  db_prepare(&q,
    "SELECT filename.name, blob.uuid, datetime(event.mtime%s),"
    "       coalesce(euser,user), coalesce(ecomment,comment)"
    "  FROM mlink, filename, blob, event"
    " WHERE mlink.fid=%d"
    "   AND filename.fnid=mlink.fnid"
    "   AND event.objid=mlink.mid"
    "   AND blob.rid=mlink.mid"
    " ORDER BY event.mtime DESC /*sort*/",
    timeline_utc(), rid);
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("file:       %s\n", db_column_text(&q,0));
    fossil_print("            part of [%.10s] by %s on %s\n",
      db_column_text(&q, 1),
      db_column_text(&q, 3),
      db_column_text(&q, 2));
    fossil_print("            ");
    comment_print(db_column_text(&q,4), 12, 78);
  }
  db_finalize(&q);

  /* Check to see if this object is used as an attachment */
  db_prepare(&q,
    "SELECT attachment.filename,"
    "       attachment.comment,"
    "       attachment.user,"
    "       datetime(attachment.mtime%s),"
    "       attachment.target,"
    "       CASE WHEN EXISTS(SELECT 1 FROM tag WHERE tagname=('tkt-'||target))"
    "            THEN 'ticket'"
    "       WHEN EXISTS(SELECT 1 FROM tag WHERE tagname=('wiki-'||target))"
    "            THEN 'wiki' END,"
    "       attachment.attachid,"
    "       (SELECT uuid FROM blob WHERE rid=attachid)"
    "  FROM attachment JOIN blob ON attachment.src=blob.uuid"
    " WHERE blob.rid=%d",
    timeline_utc(), rid
  );
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("attachment: %s\n", db_column_text(&q,0));
    fossil_print("            attached to %s %s\n",
                 db_column_text(&q,5), db_column_text(&q,4));
    if( verboseFlag ){
      fossil_print("            via %s (%d)\n",
                   db_column_text(&q,7), db_column_int(&q,6));
    }else{
      fossil_print("            via %s\n",
                   db_column_text(&q,7));
    }
    fossil_print("            by user %s on %s\n",
                 db_column_text(&q,2), db_column_text(&q,3));
    fossil_print("            ");
    comment_print(db_column_text(&q,1), 12, 78);
  }
  db_finalize(&q);
}

/*
** COMMAND: whatis*
** Usage: %fossil whatis NAME
**
** Resolve the symbol NAME into its canonical 40-character SHA1-hash
** artifact name and provide a description of what role that artifact
** plays.
*/
void whatis_cmd(void){
  int rid;
  const char *zName;
  int verboseFlag;
  db_find_and_open_repository(0,0);
  verboseFlag = find_option("verbose","v",0)!=0;
  if( g.argc!=3 ) usage("whatis NAME");
  zName = g.argv[2];
  rid = symbolic_name_to_rid(zName, 0);
  if( rid<0 ){
    Stmt q;
    int cnt = 0;
    fossil_print("Ambiguous artifact name prefix: %s\n", zName);
    db_prepare(&q,
       "SELECT rid FROM blob WHERE uuid>=lower(%Q) AND uuid<(lower(%Q)||'z')",
       zName, zName
    );
    while( db_step(&q)==SQLITE_ROW ){
      if( cnt++ ) fossil_print("%.79c\n", '-');
      whatis_rid(db_column_int(&q, 0), verboseFlag);
    }
    db_finalize(&q);
  }else if( rid==0 ){
    fossil_print("Unknown artifact: %s\n", zName);
  }else{
    whatis_rid(rid, verboseFlag);
  }
}

/*
** COMMAND: test-whatis-all
** Usage: %fossil test-whatis-all
**
** Show "whatis" information about every artifact in the repository
*/
void test_whatis_all_cmd(void){
  Stmt q;
  int cnt = 0;
  db_find_and_open_repository(0,0);
  db_prepare(&q, "SELECT rid FROM blob ORDER BY rid");
  while( db_step(&q)==SQLITE_ROW ){
    if( cnt++ ) fossil_print("%.79c\n", '-');
    whatis_rid(db_column_int(&q,0), 1);
  }
  db_finalize(&q);
}


/*
** COMMAND: test-ambiguous
** Usage: %fossil test-ambiguous [--minsize N]
**
** Show a list of ambiguous SHA1-hash abbreviations of N characters or
** more where N defaults to 4.  Change N to a different value using
** the "--minsize N" command-line option.
*/
void test_ambiguous_cmd(void){
  Stmt q, ins;
  int i;
  int minSize = 4;
  const char *zMinsize;
  char zPrev[100];
  db_find_and_open_repository(0,0);
  zMinsize = find_option("minsize",0,1);
  if( zMinsize && atoi(zMinsize)>0 ) minSize = atoi(zMinsize);
  db_multi_exec("CREATE TEMP TABLE dups(uuid, cnt)");
  db_prepare(&ins,"INSERT INTO dups(uuid) VALUES(substr(:uuid,1,:cnt))");
  db_prepare(&q,
    "SELECT uuid FROM blob "
    "UNION "
    "SELECT substr(tagname,7) FROM tag WHERE tagname GLOB 'event-*' "
    "UNION "
    "SELECT tkt_uuid FROM ticket "
    "ORDER BY 1"
  );
  zPrev[0] = 0;
  while( db_step(&q)==SQLITE_ROW ){
    const char *zUuid = db_column_text(&q, 0);
    for(i=0; zUuid[i]==zPrev[i] && zUuid[i]!=0; i++){}
    if( i>=minSize ){
      db_bind_int(&ins, ":cnt", i);
      db_bind_text(&ins, ":uuid", zUuid);
      db_step(&ins);
      db_reset(&ins);
    }
    sqlite3_snprintf(sizeof(zPrev), zPrev, "%s", zUuid);
  }
  db_finalize(&ins);
  db_finalize(&q);
  db_prepare(&q, "SELECT uuid FROM dups ORDER BY length(uuid) DESC, uuid");
  while( db_step(&q)==SQLITE_ROW ){
    fossil_print("%s\n", db_column_text(&q, 0));
  }
  db_finalize(&q);
}
