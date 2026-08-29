#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

// Retail HG/SS object-event graphics IDs (SPRITE_*) are a separate namespace
// from the members of a/0/8/1 (MMODEL_*).  Object events store the former.
// The renderer must resolve them before touching the mmodel NARC.
//
// Values here mirror pret/pokeheartgold include/constants/sprites.h and
// include/constants/mmodel.h.  Do not add a "sprite id == member id" fallback:
// it silently draws unrelated people/objects on most maps.
namespace hgss {

constexpr std::uint16_t kSpriteVarBase = 100;
constexpr std::uint16_t kSpriteVarCount = 16;
constexpr std::uint16_t kObjGfxVarBase = 0x4020;

inline bool isDynamicSprite(std::uint16_t spriteId) {
    return spriteId >= kSpriteVarBase && spriteId < kSpriteVarBase + kSpriteVarCount;
}

inline std::optional<std::uint16_t> resolveDynamicSprite(
    std::uint16_t spriteId,
    const std::unordered_map<std::uint16_t, std::uint16_t>& vars) {
    if (!isDynamicSprite(spriteId)) return spriteId;
    const auto varId = std::uint16_t(kObjGfxVarBase + (spriteId - kSpriteVarBase));
    auto it = vars.find(varId);
    if (it == vars.end()) return std::nullopt;
    // Retail object-GFX vars contain another SPRITE_* graphics ID.  Refuse a
    // recursive variable slot rather than accidentally treating it as an NARC index.
    if (isDynamicSprite(it->second)) return std::nullopt;
    return it->second;
}

inline int mmodelMemberForSprite(std::uint16_t s) {
    // Gen IV base overworld people/objects.
    switch (s) {
        case 0: return 69;  case 1: return 0;   case 2: return 1;
        case 3: return 2;   case 4: return 3;   case 5: return 4;
        case 6: return 5;   case 7: return 6;   case 8: return 7;
        case 9: return 8;   case 10:return 9;   case 11:return 10;
        case 12:return 12;  case 13:return 13;  case 14:return 14;
        case 15:return 16;  case 16:return 17;  case 17:return 18;
        case 18:return 19;  case 19:return 34;  case 20:return 35;
        case 21:return 71;  case 22:return 31;  case 23:return 32;
        case 24:return 23;  case 25:return 24;  case 29:return 25;
        case 30:return 26;  case 31:return 36;  case 33:return 22;
        case 34:return 37;  case 35:return 38;  case 36:return 39;
        case 37:return 40;  case 38:return 20;  case 39:return 21;
        case 40:return 41;  case 41:return 42;  case 42:return 43;
        case 43:return 44;  case 44:return 45;  case 45:return 46;
        case 50:return 47;  case 51:return 48;  case 52:return 49;
        case 53:return 50;  case 54:return 51;  case 55:return 52;
        case 56:return 53;  case 59:return 29;  case 60:return 30;
        case 62:return 27;  case 63:return 28;  case 68:return 11;
        case 69:return 15;  case 70:return 33;  case 71:return 68;
        case 84:return 91;  case 85:return 92;  case 86:return 93;
        case 87:return 94;  case 97:return 70;  case 98:return 72;
        case 99:return 54;

        // Kanto leaders and named legacy characters.
        case 126:return 215; case 127:return 216; case 128:return 217; case 129:return 218;
        case 130:return 219; case 131:return 220; case 132:return 221; case 133:return 222;
        case 141:return 62;  case 142:return 63;  case 143:return 64;  case 144:return 65;
        case 145:return 66;  case 146:return 67;  case 148:return 58;  case 168:return 57;
        case 169:return 61;  case 175:return 55;  case 176:return 75;  case 177:return 76;
        case 178:return 73;  case 179:return 74;  case 180:return 77;  case 181:return 78;
        case 183:return 0;   // BABYBOY1 alias
        case 188:return 79;  case 189:return 80;  case 193:return 56;
        case 196:return 85;  case 197:return 86;  case 198:return 87;  case 199:return 88;
        case 200:return 89;  case 201:return 90;  case 210:return 239; case 211:return 170;
        case 218:return 171; case 219:return 83;  case 220:return 172; case 221:return 173;
        case 222:return 174; case 223:return 175; case 224:return 176; case 225:return 177;
        case 227:return 178; case 229:return 179; case 232:return 180; case 233:return 223;
        case 234:return 242; case 235:return 181; case 236:return 182; case 237:return 183;
        case 238:return 213; case 239:return 214; case 248:return 81;  case 249:return 82;
        case 250:return 84;

        // Gate graphics (251/252/254/255) are not BMD/BTX members of a/0/8/1.
        // They are deliberately unresolved here instead of being rendered as a random NPC.
        case 253:return 0; case 256:return 0; case 257:return 0; // BABYBOY1 aliases
        case 258:return 95; case 259:return 96; case 260:return 97; case 261:return 98;
        case 262:return 251; case 263:return 255; case 264:return 258; case 265:return 252;
        case 266:return 254; case 267:return 257; case 268:return 256; case 269:return 253;
        case 270:return 262; case 271:return 265; case 272:return 259; case 273:return 261;
        case 274:return 264; case 275:return 263; case 276:return 260;
        case 277:return 103; case 278:return 104; case 279:return 105; case 280:return 106;
        case 281:return 107; case 282:return 108; case 283:return 109; case 284:return 110;
        case 285:return 101; case 286:return 102; case 287:return 111;
        case 288:return 0; case 289:return 0; // BABYBOY1 aliases
        case 290:return 112;
        case 291:return 0; case 292:return 0; // BABYBOY1 aliases
        case 293:return 244; case 294:return 245; case 295:return 246; case 296:return 247;
        case 297:return 248; case 298:return 249; case 299:return 250;

        // HG/SS-specific NPC cast.
        case 315:return 113; case 316:return 114; case 317:return 115; case 318:return 116;
        case 319:return 117; case 320:return 118; case 321:return 119; case 322:return 120;
        case 323:return 121; case 324:return 122; case 325:return 123; case 326:return 124;
        case 327:return 125; case 328:return 126; case 329:return 127; case 330:return 128;
        case 331:return 129; case 332:return 130; case 333:return 131; case 334:return 132;
        case 335:return 133; case 336:return 134; case 337:return 135; case 338:return 136;
        case 341:return 137; case 342:return 138; case 343:return 139; case 344:return 140;
        case 345:return 141; case 346:return 142; case 347:return 143; case 348:return 144;
        case 349:return 243; case 350:return 229; case 351:return 145;
        case 352:return 146; case 353:return 147; case 354:return 148; case 355:return 149;
        case 356:return 150; case 357:return 151; case 358:return 152; case 359:return 153;
        case 360:return 154; case 361:return 155; case 362:return 156; case 363:return 157;
        case 364:return 158; case 365:return 159; case 366:return 160; case 367:return 161;
        case 368:return 162; case 369:return 163; case 370:return 164; case 371:return 165;
        case 372:return 166; case 373:return 167; case 374:return 168; case 375:return 169;
        case 376:return 230; case 377:return 184; case 378:return 185; case 379:return 231;
        case 380:return 224; case 381:return 186; case 382:return 187; case 383:return 188;
        case 384:return 189; case 385:return 225; case 386:return 226; case 387:return 227;
        case 389:return 190; case 390:return 228; case 392:return 191; case 393:return 192;
        case 394:return 193; case 395:return 194; case 396:return 241; case 397:return 240;
        case 398:return 237; case 399:return 234; case 400:return 235; case 401:return 236;
        case 402:return 238; case 403:return 232; case 404:return 233; case 406:return 195;
        case 407:return 99;  case 408:return 100; case 409:return 200; case 410:return 198;
        case 411:return 197; case 412:return 199; case 413:return 196; case 414:return 209;
        case 415:return 201; case 416:return 202; case 417:return 203; case 418:return 204;
        case 419:return 205; case 420:return 206; case 421:return 59;  case 422:return 60;
        case 423:return 207; case 424:return 208; case 425:return 210; case 426:return 211;
        case 427:return 212;
        default: break;
    }

    // The regular follower-mon blocks are parallel tables in retail.
    if (s >= 428 && s <= 993) return int(s) - 131;

    // Static encounter/follower graphics use the same mmodel as their regular
    // follower species.  They are separate SPRITE_* IDs because their field behavior differs.
    switch (s) {
        case 994:return 309; case 995:return 313; case 996:return 316; case 997:return 318;
        case 998:return 319; case 999:return 322; case 1000:return 327; case 1001:return 330;
        case 1002:return 331; case 1003:return 333; case 1004:return 337; case 1005:return 339;
        case 1006:return 341; case 1007:return 348; case 1008:return 350; case 1009:return 351;
        case 1010:return 352; case 1011:return 360; case 1012:return 361; case 1013:return 364;
        case 1014:return 365; case 1015:return 377; case 1016:return 378; case 1017:return 381;
        case 1018:return 383; case 1019:return 399; case 1020:return 410; case 1021:return 411;
        case 1022:return 413; case 1023:return 429; case 1024:return 442; case 1025:return 443;
        case 1026:return 444; case 1027:return 445; case 1028:return 447; case 1029:return 448;
        case 1030:return 451; case 1031:return 481; case 1032:return 483; case 1033:return 490;
        case 1034:return 498; case 1035:return 571; case 1036:return 572; case 1037:return 573;
        case 1038:return 574; case 1039:return 575; case 1040:return 581; case 1041:return 710;
        case 1042:return 711; case 1043:return 712; case 1044:return 713; case 1045:return 714;
        case 1046:return 472; case 1047:return 452; case 1048:return 456; case 1049:return 459;
        default:return -1;
    }
}

inline int resolveMmodelMember(
    std::uint16_t eventSpriteId,
    const std::unordered_map<std::uint16_t, std::uint16_t>& vars,
    std::uint16_t* resolvedSpriteId = nullptr) {
    auto resolved = resolveDynamicSprite(eventSpriteId, vars);
    if (!resolved) return -1;
    if (resolvedSpriteId) *resolvedSpriteId = *resolved;
    return mmodelMemberForSprite(*resolved);
}

} // namespace hgss
