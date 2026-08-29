#pragma once

// HG/SS mmodel player textures are grouped as four consecutive frames per
// direction: Up 1-4, Down 5-8, Left 9-12, Right 13-16.
// v0.16 only selected two of those frames while moving, which meant one leg
// never appeared in the walk cycle.
inline int hg_walk_base_frame(int direction){
    switch(direction){case 3:return 1;case 0:return 5;case 1:return 9;case 2:return 13;default:return 5;}
}
inline int hg_walk_phase(double seconds){
    int frame=int(seconds*8.0);
    return frame&3;
}
inline int hg_walk_frame_number(int direction,bool moving,int phase){
    return hg_walk_base_frame(direction)+(moving?(phase&3):0);
}

// HG/SS follower Pokemon use a compact eight-frame texture set rather than the
// player's 16-frame layout.  The authored texture suffixes are:
//   Up .1/.10, Down .11/.12, Left .13/.14, Right .15/.16.
// Each pair alternates while moving; the first frame is the idle pose.
inline int hg_follower_frame_number(int direction,bool moving,int phase){
    int a=11,b=12;
    switch(direction){
        case 3:a=1;b=10;break;  // Up
        case 0:a=11;b=12;break; // Down
        case 1:a=13;b=14;break; // Left
        case 2:a=15;b=16;break; // Right
        default:break;
    }
    return moving&&((phase&1)!=0)?b:a;
}
