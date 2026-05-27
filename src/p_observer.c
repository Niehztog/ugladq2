//===========================================================================
//
// Name:				p_observer.c
// Function:		observer mode
// Programmer:		Mr Elusive (MrElusive@demigod.demon.nl)
// Last update:	1998-01-12
// Tab Size:		3
//
// Reconstructed from the 1999 Win32 gamex86.dll (the only release where this
// file was compiled in -- the public source release omitted it).  Behaviour
// matches the disassembly at addresses 0x1007a2e7..0x1007c174 in
// reference/gamex86.dll.  The client-facing observer surface
// (ClientPlaceCamera, ClientCycleCamera, ClientSetCamera, DoObserver,
// ClientToggleObserver / ClientToggleAutoCam / ClientToggleChaseCam /
// ClientToggleCameraFixed / ClientToggleCameraName, ClientObserverHelp,
// ClientObserverCmd) is reconstructed line-by-line from x86 disasm of
// sub_1007b56a..sub_1007c043.
//
//===========================================================================

#include "g_local.h"
#include "p_observer.h"

#ifdef OBSERVER

extern cvar_t *observer;
extern void SelectSpawnPoint(edict_t *ent, vec3_t origin, vec3_t angles);

//===========================================================================
// AngleDifference                                            sub_10077eba
//
// Public per the original p_observer.h from gladq2_src.  The shipping
// gamex86.dll has exactly ONE copy of this routine in .text at
// 0x10077eba; earlier drafts of this reconstruction carried a second
// "SubAngleDifference" alias which has now been folded into this one.
//
// Returns the signed shortest-arc difference (ang1 - ang2), wrapped to
// the range [-180, 180].  Reconstructed line-by-line from the disasm.
//===========================================================================
float AngleDifference(float ang1, float ang2)
{
	float diff;

	diff = ang1 - ang2;
	if (ang1 > ang2)
	{
		if (diff >  180.0) diff -= 360.0;
	}
	else
	{
		if (diff < -180.0) diff += 360.0;
	}
	return diff;
} //end of the function AngleDifference

//===========================================================================
// Forward declarations (the dispatcher and DoObserver call functions
// defined further down in this file; their reconstructions live near the
// bottom, in the original source order).  Kept here so the linker
// notices the cross-references inside ClientCycleCamera/ClientSetCamera/
// ClientToggleAutoCam/ClientToggleChaseCam which all call
// ClientToggleObserver as their first action.
//===========================================================================
void ClientToggleObserver(edict_t *ent);
static void CameraPlaceAtTarget(edict_t *ent, vec3_t end, vec3_t out_angles, vec3_t cmdangles);
static void SubLerpAngle(float *out, vec3_t target, float frac, float maxstep);

//===========================================================================
// ClientPlaceCamera                                          sub_1007b56a
//
// Snapshot the observer's own viewangles+origin into cam->clientangles/
// cam->clientorigin while the camera is targeting the observer himself,
// or hand the camera back to him (calling CameraPlaceAtTarget) when the
// current target is gone / no longer an observer.
//===========================================================================
static void ClientPlaceCamera(edict_t *ent)
{
	camera_t *cam;

	cam = &ent->client->camera;
	if (cam->ent == ent)
	{
		cam->clientangles[0] = ent->client->ps.viewangles[0];
		cam->clientangles[1] = ent->client->ps.viewangles[1];
		cam->clientangles[2] = ent->client->ps.viewangles[2];
		cam->clientorigin[0] = ent->s.origin[0];
		cam->clientorigin[1] = ent->s.origin[1];
		cam->clientorigin[2] = ent->s.origin[2];
	} //end if
	else if (!cam->ent || !cam->ent->inuse || !(cam->ent->flags & FL_OBSERVER))
	{
		cam->ent = ent;
		CameraPlaceAtTarget(ent,
			cam->clientorigin,
			cam->clientangles,
			/* sub_10077f7d cmdangles arg -- gclient_t resp.cmd_angles at +0xdc8;
			   the most-recent received cmd-angles vector. */
			ent->client->resp.cmd_angles);
	} //end else if
	/* else: current target is a valid observer client; leave camera alone */
} //end of the function ClientPlaceCamera

//===========================================================================
// ClientCycleCamera                                          sub_1007b652
//
// "cyclecam" command: walk through the clients starting at the slot
// after the current target and lock onto the first in-use, non-observer
// one.  Falls back to ClientToggleObserver / ClientPlaceCamera and
// refuses to do anything while CAMFL_AUTOCAM is set.
//===========================================================================
void ClientCycleCamera(edict_t *ent)
{
	camera_t *cam;
	edict_t *cand;
	int i;
	int idx;

	if (!(ent->flags & FL_OBSERVER))
		ClientToggleObserver(ent);

	cam = &ent->client->camera;
	if (cam->flags & CAMFL_AUTOCAM)
	{
		gi.cprintf(ent, PRINT_HIGH, "cyclecam not available in autocam mode\n");
		return;
	} //end if

	ClientPlaceCamera(ent);

	idx = (cam->ent - g_edicts) - 1;
	for (i = 0; i < game.maxclients; i++)
	{
		idx++;
		if (idx >= game.maxclients) idx = 0;
		cand = g_edicts + 1 + idx;
		if (!cand->inuse) continue;
		if ((cand->flags & FL_OBSERVER) && cand != ent) continue;
		cam->ent = cand;
		break;
	} //end for

	if (i == game.maxclients)
		gi.cprintf(ent, PRINT_HIGH, "no valid client found to observe\n");

	if (cam->ent == ent)
		CameraPlaceAtTarget(ent,
			cam->clientorigin,
			cam->clientangles,
			ent->client->resp.cmd_angles);
} //end of the function ClientCycleCamera

//===========================================================================
// ClientSetCamera                                            sub_1007b7bd
//
// "setcam <name>" command: target an in-use client whose pers.netname
// matches the first command argument.  Behaves like ClientCycleCamera
// for the prelude (FL_OBSERVER auto-enter, CAMFL_AUTOCAM refusal,
// ClientPlaceCamera snapshot).
//===========================================================================
void ClientSetCamera(edict_t *ent)
{
	camera_t *cam;
	edict_t *cand;
	int i;
	char *name;

	if (!(ent->flags & FL_OBSERVER))
		ClientToggleObserver(ent);

	cam = &ent->client->camera;
	if (cam->flags & CAMFL_AUTOCAM)
	{
		gi.cprintf(ent, PRINT_HIGH, "setcam not available in autocam mode\n");
		return;
	} //end if

	if (gi.argc() <= 1)
	{
		gi.cprintf(ent, PRINT_HIGH, "usage: setcam <client name>\n");
		return;
	} //end if

	ClientPlaceCamera(ent);
	name = gi.argv(1);

	for (i = 0; i < game.maxclients; i++)
	{
		cand = g_edicts + 1 + i;
		if (!cand->inuse) continue;
		if (!strcmp(name, cand->client->pers.netname))
		{
			cam->ent = cand;
			break;
		} //end if
	} //end for

	if (i == game.maxclients)
		gi.cprintf(ent, PRINT_HIGH, "no valid client found with the name %s\n", name);

	if (cam->ent == ent)
		CameraPlaceAtTarget(ent,
			cam->clientorigin,
			cam->clientangles,
			ent->client->resp.cmd_angles);
} //end of the function ClientSetCamera

//===========================================================================
// ClientToggleObserver                                       sub_1007bb4c
//
// "observer" command toggle.  Note the original tests "deathmatch ==
// 0" first (in which case respawn() is called -- single-player drops
// back into normal play immediately), and only then "observer == 0"
// (which forbids leaving observer mode in a deathmatch where the cvar
// was disabled).
//===========================================================================
void ClientToggleObserver(edict_t *ent)
{
	if (ent->flags & FL_OBSERVER)
	{
		// ---- leaving observer mode ----
		if (deathmatch->value == 0)
		{
			respawn(ent);
			return;
		} //end if
		if (observer->value == 0)
		{
			gi.cprintf(ent, PRINT_HIGH, "can't leave observer mode\n");
			return;
		} //end if
		ent->classname = "player";
		ent->flags &= ~FL_OBSERVER;
		ent->solid = SOLID_BBOX;
		ent->takedamage = DAMAGE_AIM;
		ent->svflags &= ~SVF_NOCLIENT;
		PutClientInServer(ent);
		// teleport-style spawn-in effect
		ent->client->ps.pmove.pm_flags = PMF_TIME_TELEPORT;
		ent->client->ps.pmove.pm_time = 14;
		ent->client->ps.gunindex = gi.modelindex(ent->client->pers.weapon->view_model);
		ent->movetype = MOVETYPE_WALK;
		gi.WriteByte(svc_muzzleflash);
		gi.WriteShort(ent - g_edicts);
		gi.WriteByte(MZ_LOGIN);
		gi.multicast(ent->s.origin, MULTICAST_PVS);
		gi.bprintf(PRINT_HIGH, "%s left observer mode\n", ent->client->pers.netname);
	} //end if
	else
	{
		// ---- entering observer mode ----
		ent->classname = "observer";
		ent->flags |= FL_OBSERVER;
		ent->solid = SOLID_NOT;
		ent->takedamage = DAMAGE_NO;
		ent->movetype = MOVETYPE_NOCLIP;
		ent->health = 100;
		ent->client->ps.stats[STAT_HEALTH] = ent->health;
		ent->svflags |= SVF_NOCLIENT;
		ent->client->camera.ent = ent;
		ClientPlaceCamera(ent);
		if (deathmatch->value)
			ent->client->resp.score = 0;
		gi.bprintf(PRINT_HIGH, "%s entered observer mode\n", ent->client->pers.netname);
	} //end else
} //end of the function ClientToggleObserver

//===========================================================================
// ClientToggleCameraFixed                                    sub_1007bda4
//
// "camfixed" command: flip CAMFL_FIXED and tell the player whether the
// chase-cam offset is now "fixed" or "variable".
//===========================================================================
void ClientToggleCameraFixed(edict_t *ent)
{
	ent->client->camera.flags ^= CAMFL_FIXED;
	gi.cprintf(ent, PRINT_HIGH, "camera offsets are ");
	if (ent->client->camera.flags & CAMFL_FIXED)
		gi.cprintf(ent, PRINT_HIGH, "fixed\n");
	else
		gi.cprintf(ent, PRINT_HIGH, "variable\n");
} //end of the function ClientToggleCameraFixed

//===========================================================================
// ClientToggleCameraName                                     sub_1007be15
//
// "camname" command: flip CAMFL_NAME (whether the tracked player's
// name is shown on the chase camera).
//===========================================================================
static void ClientToggleCameraName(edict_t *ent)
{
	ent->client->camera.flags ^= CAMFL_NAME;
	gi.cprintf(ent, PRINT_HIGH, "camera player names ");
	if (ent->client->camera.flags & CAMFL_NAME)
		gi.cprintf(ent, PRINT_HIGH, "on\n");
	else
		gi.cprintf(ent, PRINT_HIGH, "off\n");
} //end of the function ClientToggleCameraName

//===========================================================================
// ClientToggleAutoCam                                        sub_1007be86
//
// "autocam" command toggle.  When enabling, snapshot the observer's
// current position into cam->dest, fire AngleVectors() through the
// observer's v_angle to seed cam->dest+0xc..0x14 (a forward vector
// scaled by 100.0 -- the BE85b constant 0x42c80000 = 100.0f), point
// the camera at the observer himself and start the autocam state
// machine on a fresh frame.
//===========================================================================
static void ClientToggleAutoCam(edict_t *ent)
{
	camera_t *cam;
	vec3_t forward;

	if (!(ent->flags & FL_OBSERVER))
		ClientToggleObserver(ent);

	cam = &ent->client->camera;
	cam->flags ^= CAMFL_AUTOCAM;
	gi.cprintf(ent, PRINT_HIGH, "autocam ");
	if (cam->flags & CAMFL_AUTOCAM)
	{
		cam->dest[0] = ent->s.origin[0];
		cam->dest[1] = ent->s.origin[1];
		cam->dest[2] = ent->s.origin[2];
		AngleVectors(ent->client->v_angle, forward, NULL, NULL);
		VectorMA(cam->dest, 100.0, forward, cam->viewtarget);
		cam->ent = ent;
		cam->pause_time = level.time;
		cam->delay = level.time;
		gi.cprintf(ent, PRINT_HIGH, "on\n");
	} //end if
	else
	{
		gi.cprintf(ent, PRINT_HIGH, "off\n");
	} //end else
} //end of the function ClientToggleAutoCam

//===========================================================================
// ClientToggleChaseCam                                       sub_1007bfae
//
// "chasecam" command: enter observer mode if necessary, then flip
// CAMFL_CHASECAM.
//===========================================================================
static void ClientToggleChaseCam(edict_t *ent)
{
	if (!(ent->flags & FL_OBSERVER))
		ClientToggleObserver(ent);

	ent->client->camera.flags ^= CAMFL_CHASECAM;
	if (ent->client->camera.flags & CAMFL_CHASECAM)
		gi.cprintf(ent, PRINT_HIGH, "chasecam on\n");
	else
		gi.cprintf(ent, PRINT_HIGH, "chasecam off\n");
} //end of the function ClientToggleChaseCam

//===========================================================================
// ClientObserverHelp                                         sub_1007c02a
//
// "observerhelp" command: dump the observer command summary at
// PRINT_HIGH.  The original is a single gi.cprintf into one big
// string at 0x100c04f0; we keep the C-string-concatenation style.
//===========================================================================
void ClientObserverHelp(edict_t *ent)
{
	gi.cprintf(ent, PRINT_HIGH,
		"observer        toggles observer mode\n"
		"autocam         toggle auto targeting camera mode\n"
		"chasecam        toggle manually positioned chase cam\n"
		"cyclecam        cycles camera to next player\n"
		"setcam <name>   set camera to player with name\n"
		"camfixed        toggle chase camera offset fixed\n"
		"camname         toggle showing name of tracked player\n"
		"observerhelp    this help message\n");
} //end of the function ClientObserverHelp

//===========================================================================
// Autocam state handlers and CameraMove / CameraInputThink / CameraPlaceAtTarget
//
// These helpers live in the original DLL at the addresses noted below.
// They are now fully reconstructed below; these are the dispatch-order
// forward declarations.
//===========================================================================
static void CameraAutoCamState0(edict_t *ent, usercmd_t *ucmd);   /* sub_1007a2e7 */
static void CameraAutoCamState1(edict_t *ent, usercmd_t *ucmd);   /* sub_10079f4d */
static void CameraAutoCamState2(edict_t *ent, usercmd_t *ucmd);   /* sub_10079f64 */
static void CameraAutoCamState3(edict_t *ent, usercmd_t *ucmd);   /* sub_1007a1d2 */
static void CameraAutoCamState7(edict_t *ent, usercmd_t *ucmd);   /* sub_10079c26 */
static void CameraMove(edict_t *ent, float speed, usercmd_t *ucmd); /* sub_100784f9 */
static void CameraInputThink(edict_t *ent, usercmd_t *ucmd);      /* sub_1007ace3 */
static void CameraPlaceAtTarget(edict_t *ent, vec3_t end, vec3_t out_angles, vec3_t cmdangles); /* sub_10078043 */

//===========================================================================
// CameraAutoCamThink
//
// Per-frame state machine for autocam mode.  Reconstructed faithfully from
// gamex86.dll @ 0x1007abd5: a switch on cam->state that calls one of five
// state handlers followed by a uniform CameraMove(ent, speed, ucmd) helper.
// Unknown states reset cam->state to 0.
//===========================================================================
static void CameraAutoCamThink(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam;

	cam = &ent->client->camera;
	if (cam->state == 0)
	{
		CameraAutoCamState0(ent, ucmd);
		CameraMove(ent, 1.0, ucmd);
	} //end if
	else if (cam->state == 2)
	{
		CameraAutoCamState2(ent, ucmd);
		CameraMove(ent, 0.0, ucmd);
	} //end else if
	else if (cam->state == 3)
	{
		CameraAutoCamState3(ent, ucmd);
		CameraMove(ent, 0.0, ucmd);
	} //end else if
	else if (cam->state == 7)
	{
		CameraAutoCamState7(ent, ucmd);
		CameraMove(ent, 100.0, ucmd);
	} //end else if
	else if (cam->state == 1)
	{
		CameraAutoCamState1(ent, ucmd);
		CameraMove(ent, 0.0, ucmd);
	} //end else if
	else
	{
		cam->state = 0;
	} //end else
} //end of the function CameraAutoCamThink

//===========================================================================
// CameraChaseCamThink
//
// Per-frame chase-cam logic.  Reconstructed faithfully from gamex86.dll @
// 0x1007b05d.  Steps:
//   1. If !CAMFL_FIXED, let user input adjust the camera (CameraInputThink).
//   2. Snap (CAMFL_NOSMOOTHING) or LerpAngles cam->ent_angles toward the
//      chase target's v_angle, then refresh cam->lasttime.
//   3. Compute a yaw-offset forward vector (with the pitch correction
//      forward[2] = -(forward[0]*up[0] + forward[1]*up[1]) / up[2]),
//      normalise it, and build a 'dir' vector of length -chaseoffset[PITCH]
//      offset by chaseoffset[ROLL] on Z.
//   4. cam->origin = target.origin + chaseoffset.
//   5. Trace from cam->origin out by 'dir' through MASK_OPAQUE, ignoring
//      the chase target, then push the endpoint 5 units further along
//      forward (so the camera does not clip into the wall it hit).
//   6. Build cmdangles from ucmd->angles via SHORT2ANGLE and hand off to
//      CameraPlaceAtTarget which writes the final angles back; copy those
//      into cam->angles.
//===========================================================================
static void CameraChaseCamThink(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam;
	vec3_t   angles, forward, up, dir, end, cmdangles;
	trace_t  tr;

	if (!(ent->client->camera.flags & CAMFL_FIXED))
		CameraInputThink(ent, ucmd);

	cam = &ent->client->camera;
	if (cam->flags & CAMFL_NOSMOOTHING)
	{
		VectorCopy(cam->ent->client->v_angle, cam->ent_angles);
	} //end if
	else
	{
		// Lerp ent_angles toward target's v_angle by (level.time - lasttime),
		// clamped by max-step 1.0.  Inlined to avoid depending on an
		// engine helper that isn't in q_shared.
		float frac = level.time - cam->lasttime;
		if (frac < 0) frac = 0;
		if (frac > 1.0) frac = 1.0;
		cam->ent_angles[0] += frac * AngleDifference(cam->ent->client->v_angle[0], cam->ent_angles[0]);
		cam->ent_angles[1] += frac * AngleDifference(cam->ent->client->v_angle[1], cam->ent_angles[1]);
		cam->ent_angles[2] += frac * AngleDifference(cam->ent->client->v_angle[2], cam->ent_angles[2]);
	} //end else
	cam->lasttime = level.time;

	// Build working angles from ent_angles, snag the 'up' vector
	VectorCopy(cam->ent_angles, angles);
	AngleVectors(angles, NULL, NULL, up);

	// Apply yaw chase offset, kill pitch, recompute forward
	angles[YAW] = anglemod(angles[YAW] + cam->chaseoffset[YAW]);
	angles[PITCH] = 0;
	AngleVectors(angles, forward, NULL, NULL);

	// Pitch correction: project forward onto the up plane so the camera
	// rises and falls with the target's pitch:
	//   forward[2] = -(forward[0]*up[0] + forward[1]*up[1]) / up[2]
	forward[2] = (forward[0] * up[0] + forward[1] * up[1]) * -1.0 / up[2];
	VectorNormalize(forward);

	// dir = forward * (-chaseoffset[PITCH]); dir[2] += chaseoffset[ROLL]
	VectorScale(forward, -cam->chaseoffset[PITCH], dir);
	dir[2] += cam->chaseoffset[ROLL];

	// cam->origin = target.origin + chaseoffset
	cam->origin[0] = cam->ent->s.origin[0] + cam->chaseoffset[0];
	cam->origin[1] = cam->ent->s.origin[1] + cam->chaseoffset[1];
	cam->origin[2] = cam->ent->s.origin[2] + cam->chaseoffset[2];

	// end = cam->origin + dir
	end[0] = cam->origin[0] + dir[0];
	end[1] = cam->origin[1] + dir[1];
	end[2] = cam->origin[2] + dir[2];

	// Trace from cam->origin to end, ignoring the chase target itself.
	// MASK_OPAQUE = 0x19 in the original disassembly.
	tr = gi.trace(cam->origin, vec3_origin, vec3_origin, end, cam->ent, MASK_OPAQUE);

	// Push the endpoint 5 units further along the (re-normalised) forward
	// vector so the camera doesn't sit flush against the wall it hit.
	VectorNormalize2(forward, angles);   // disasm calls VectorNormalize2(forward, &angles[0])
	VectorScale(forward, 5.0, forward);
	end[0] = tr.endpos[0] + forward[0];
	end[1] = tr.endpos[1] + forward[1];
	end[2] = tr.endpos[2] + forward[2];

	// Convert ucmd->angles (short) -> cmdangles (float) via SHORT2ANGLE.
	cmdangles[0] = SHORT2ANGLE(ucmd->angles[0]);
	cmdangles[1] = SHORT2ANGLE(ucmd->angles[1]);
	cmdangles[2] = SHORT2ANGLE(ucmd->angles[2]);

	// Final placement -- writes resulting angles back through 'angles'.
	CameraPlaceAtTarget(ent, end, angles, cmdangles);

	// cam->angles = angles  (the values written by CameraPlaceAtTarget)
	VectorCopy(angles, cam->angles);
} //end of the function CameraChaseCamThink

//===========================================================================
// CameraFixedCamThink                                        sub_1007b363
//
// Dead code in the shipping DLL -- the byte pattern of its address
// (63 b3 07 10) appears nowhere else in gamex86.dll, so this function is
// never reached.  Reconstructed here line-by-line from the disassembly
// for archival completeness; kept static so the link is silent if it
// stays unreferenced.  The shape is a "fixed chase camera" variant: it
// snaps/lerps the cam's view to the target's v_angle, then pushes the
// camera forward 15 units along the horizontal view vector before
// finalising via CameraPlaceAtTarget.
//===========================================================================
static void CameraFixedCamThink(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam;
	vec3_t forward;
	vec3_t cmdangles;

	cam = &ent->client->camera;
	if (cam->flags & CAMFL_NOSMOOTHING)
	{
		cam->ent_angles[0] = cam->ent->client->v_angle[0];
		cam->ent_angles[1] = cam->ent->client->v_angle[1];
		cam->ent_angles[2] = cam->ent->client->v_angle[2];
	} //end if
	else
	{
		SubLerpAngle(cam->ent_angles, cam->ent->client->v_angle,
		             level.time - cam->lasttime, 1.0f);
	} //end else
	cam->lasttime = level.time;

	cam->origin[0] = cam->chaseoffset[0] + cam->ent->s.origin[0];
	cam->origin[1] = cam->chaseoffset[1] + cam->ent->s.origin[1];
	cam->origin[2] = cam->chaseoffset[2] + cam->ent->s.origin[2];

	AngleVectors(cam->ent_angles, forward, NULL, NULL);
	forward[2] = 0;
	VectorNormalize(forward);   // length discarded
	forward[0] *= 15.0f;
	forward[1] *= 15.0f;
	// forward[2] stays 0 (multiplied by 0 in disasm via fld [ebp-8])

	cam->origin[0] += forward[0];
	cam->origin[1] += forward[1];
	cam->origin[2] += forward[2];

	// pre-copy cam->ent_angles into cam->angles (this is overwritten again
	// after CameraPlaceAtTarget but the original does both writes verbatim)
	cam->angles[0] = cam->ent_angles[0];
	cam->angles[1] = cam->ent_angles[1];
	cam->angles[2] = cam->ent_angles[2];

	cmdangles[0] = SHORT2ANGLE(ucmd->angles[0]);
	cmdangles[1] = SHORT2ANGLE(ucmd->angles[1]);
	cmdangles[2] = SHORT2ANGLE(ucmd->angles[2]);

	CameraPlaceAtTarget(ent, cam->origin, cam->ent_angles, cmdangles);

	cam->angles[0] = cam->ent_angles[0];
	cam->angles[1] = cam->ent_angles[1];
	cam->angles[2] = cam->ent_angles[2];
} //end of the function CameraFixedCamThink

//===========================================================================
// NOTE: ClientSetViewAngles was declared in Mr. Elusive's original
// p_observer.h (gladq2_src, 1998-01-12) but its body was removed from
// p_observer.c before the 1999 shipping gamex86.dll was compiled --
// byte-pattern search of the full DLL finds no function matching the
// signature.  The header declaration is kept verbatim for archival
// fidelity to the 1998 source; no body is provided here because doing
// so would require synthesizing code with no disassembly origin.
// Nothing in the reconstructed codebase calls it, so the missing
// definition does not break the link.
//===========================================================================

//===========================================================================
// Faithful disassembly-derived reconstruction of the 5 autocam state
// handlers + CameraMove / CameraInputThink / CameraPlaceAtTarget.
//
// Translated line-by-line from gamex86.dll at:
//   sub_10078043 = CameraPlaceAtTarget   (18 lines  -> the trivial dispatch)
//   sub_10079f4d = CameraAutoCamState1   ("fixed mode" centerprint)
//   sub_1007a1d2 = CameraAutoCamState3   (target-tracking)
//   sub_10079f64 = CameraAutoCamState2   (chase from a fixed distance)
//   sub_10079c26 = CameraAutoCamState7   ("bodyque" / corpse chase)
//   sub_1007ace3 = CameraInputThink      (m_pitch / chaseoffset twiddling)
//   sub_100784f9 = CameraMove            (per-state move + angle blend)
//   sub_1007a2e7 = CameraAutoCamState0   (target selection)
//
// Constants come from .rdata at gamex86.dll+0x92000.  Inline literals
// are decoded as their float values for readability.
//
// The nested helper functions (sub_10077e29, sub_10077eba, sub_10077f15,
// sub_10077f7d, sub_1007845f, sub_100788ff, sub_1007897a, sub_10078125,
// sub_100782ad, sub_10078a04, sub_10078bcc, sub_10079a92, sub_10079acf,
// sub_10078c53 SubScoreCameraPos, sub_10078f4a SubCameraFindFlybyPos,
// sub_10085904) are all reconstructed line-by-line from the disassembly
// further down in this file.
//===========================================================================

// ---- Forward declarations for the nested helpers (defined at file bottom) --
static void  SubApplyCameraOrigin   (edict_t *ent, vec3_t origin);                     /* sub_10077f15 */
static void  SubApplyCameraAngles   (edict_t *ent, vec3_t out_angles, vec3_t cmd);     /* sub_10077f7d */
static void  SubLerpAngle           (float *out, vec3_t target, float frac, float maxstep); /* sub_10077e29 */
static qboolean SubCanSeePoint      (edict_t *ent, vec3_t point);                      /* sub_10078125 */
static qboolean SubTargetVisible    (edict_t *ent, edict_t *target);                   /* sub_100782ad */
static edict_t *SubNextClient       (edict_t *prev);                                   /* sub_1007845f */
static void  SubAbortAutocam        (edict_t *ent, usercmd_t *ucmd);                   /* sub_100788ff */
static void  SubOnTargetDeath       (edict_t *ent);                                    /* sub_1007897a */
static void  SubGetTargetMuzzle     (edict_t *ent, vec3_t out);                        /* sub_10078a04 */
static void  SubGetTargetAimEnd     (edict_t *ent, vec3_t out);                        /* sub_10078bcc */
static void  SubAutocamSetSpot      (edict_t *target, vec3_t spot);                    /* sub_10079a92 */
static void  SubSetAutocamTarget    (edict_t *ent, edict_t *target, usercmd_t *ucmd);  /* sub_10079acf */
// (sub_10085904 = q_shared.c VectorCompare, declared in q_shared.h)

// Mask value at gamex86.dll+0x10092 area, embedded as immediate 0x2010003 in
// every trace call.  Standard MASK_OPAQUE = SOLID|LAVA|SLIME|WINDOW, encoded
// as CONTENTS_SOLID(1) | CONTENTS_WINDOW(2) | CONTENTS_LAVA(8) | something.
#ifndef OBSERVER_TRACE_MASK
#define OBSERVER_TRACE_MASK 0x2010003
#endif

//===========================================================================
// sub_1007ace3 -- CameraInputThink
//===========================================================================
static void CameraInputThink(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam;
	cvar_t *m_pitch;
	float yaw_in, pitch_in, roll_in;
	float yaw_diff, pitch_diff, roll_diff;
	float m_pitch_val, inv_m_pitch;

	cam = &ent->client->camera;

	// pitch_in = anglemod( SHORT2ANGLE(ucmd->angles[PITCH]) + SHORT2ANGLE(client->ps.pmove.delta_angles[PITCH]) )
	// (gclient_t.ps.pmove.delta_angles lives at +0x14, short[3])
	pitch_in = SHORT2ANGLE(ucmd->angles[PITCH])
	         + SHORT2ANGLE(ent->client->ps.pmove.delta_angles[PITCH]);
	pitch_in = anglemod(pitch_in);

	yaw_in   = SHORT2ANGLE(ucmd->angles[YAW])
	         + SHORT2ANGLE(ent->client->ps.pmove.delta_angles[YAW]);
	yaw_in   = anglemod(yaw_in);

	roll_in  = SHORT2ANGLE(ucmd->angles[ROLL])
	         + SHORT2ANGLE(ent->client->ps.pmove.delta_angles[ROLL]);
	roll_in  = anglemod(roll_in);

	// Wrap cam->angles[*] through anglemod to keep them in [0,360).
	cam->angles[0] = anglemod(cam->angles[0]);
	cam->angles[1] = anglemod(cam->angles[1]);
	cam->angles[2] = anglemod(cam->angles[2]);

	pitch_diff = AngleDifference(pitch_in, cam->angles[0]);
	yaw_diff   = AngleDifference(yaw_in,   cam->angles[1]);
	roll_diff  = AngleDifference(roll_in,  cam->angles[2]);

	// Clamp pitch_diff into [-20, 20].
	if (pitch_diff >  20.0f) pitch_diff =  20.0f;
	else if (pitch_diff < -20.0f) pitch_diff = -20.0f;

	// inv_m_pitch = -0.022 / m_pitch  (so chaseoffset moves proportional to m_pitch)
	m_pitch = gi.cvar("m_pitch", 0, 0);
	m_pitch_val = -0.022f;
	if (m_pitch && m_pitch->value != 0.0f)
		m_pitch_val = m_pitch->value;
	inv_m_pitch = -0.022f / m_pitch_val;

	// chaseoffset[ROLL] is the "vertical" component (offset behind the player).
	// Adjust by  +/- 0.5 * pitch_diff * inv_m_pitch depending on attack-button
	// state (ucmd->buttons bit 0 == BUTTON_ATTACK).
	if (ucmd->buttons & 1)
	{
		if (pitch_diff >  3.0f)
			cam->chaseoffset[ROLL] += inv_m_pitch * pitch_diff * 0.5f;
		else if (pitch_diff < -3.0f)
			cam->chaseoffset[ROLL] += inv_m_pitch * pitch_diff * 0.5f;
	}
	else
	{
		if (pitch_diff >  3.0f)
			cam->chaseoffset[YAW] -= inv_m_pitch * pitch_diff * 0.5f;
		else if (pitch_diff < -3.0f)
			cam->chaseoffset[YAW] -= inv_m_pitch * pitch_diff * 0.5f;
	}

	// chaseoffset[PITCH] += yaw_diff * 0.2 ; wrap.
	if (yaw_diff >  10.0f || yaw_diff < -10.0f)
	{
		cam->chaseoffset[PITCH] += yaw_diff * 0.2f;
		cam->chaseoffset[PITCH]  = anglemod(cam->chaseoffset[PITCH]);
	}

	// chaseoffset[YAW] clamp into [32, 100].
	if (cam->chaseoffset[YAW] >  100.0f) cam->chaseoffset[YAW] = 100.0f;
	else if (cam->chaseoffset[YAW] < 32.0f) cam->chaseoffset[YAW] = 32.0f;

	// chaseoffset[ROLL] clamp into [-48, 48].
	if (cam->chaseoffset[ROLL] >  48.0f) cam->chaseoffset[ROLL] =  48.0f;
	else if (cam->chaseoffset[ROLL] < -48.0f) cam->chaseoffset[ROLL] = -48.0f;
} //end of the function CameraInputThink

//===========================================================================
// sub_10078043 -- CameraPlaceAtTarget
//
//   sub_10077f15(ent, cmdangles_arg);
//   sub_10077f7d(ent, end_arg, out_angles_arg);
//
// Note: arg layout from disasm is (ent, end, out_angles, cmdangles) but the
// helper SubApplyCameraOrigin's vec3 arg is the SECOND function arg (ebp+0xc).
// CameraChaseCamThink calls us with: (ent, end, &out_angles, cmdangles)
// and the disasm shows sub_10077f15(ent, ebp+0xc), i.e. with 'end'.
//===========================================================================
static void CameraPlaceAtTarget(edict_t *ent, vec3_t end, vec3_t out_angles, vec3_t cmdangles)
{
	SubApplyCameraOrigin(ent, end);                          // sub_10077f15
	SubApplyCameraAngles(ent, out_angles, cmdangles);        // sub_10077f7d
} //end of the function CameraPlaceAtTarget

//===========================================================================
// sub_100784f9 -- CameraMove
//
// 'speed': passed as second argument (the dispatcher passes 1.0, 0.0, 0.0,
//          100.0, 0.0 for states 0,1,2,7,3 respectively).
//
// Branches:
//   speed == 0       -> teleport: SubApplyCameraOrigin(ent, cam->dest)
//   else trace from cam->ent->origin to cam->dest; if hit, snap; else
//     compute step along the trace direction and advance.
//   Then per state (0 / 3 / 7 / other) compute target angle vector by
//   subtracting positions and feed into SubLerpAngle.
//===========================================================================
static void CameraMove(edict_t *ent, float speed, usercmd_t *ucmd)
{
	camera_t *cam;
	trace_t tr;
	vec3_t move;
	vec3_t target_ang;
	float dist, dt, time_to_close;
	vec3_t cmdangles;

	cam = &ent->client->camera;

	// if (speed == 0) snap to dest and skip the lerp.
	if (speed == 0.0f)
	{
		SubApplyCameraOrigin(ent, cam->dest);
		goto angles_phase;
	}

	tr = gi.trace(ent->s.origin, vec3_origin, vec3_origin, cam->dest,
	              ent, OBSERVER_TRACE_MASK);
	if (tr.fraction < 1.0f)
	{
		SubApplyCameraOrigin(ent, cam->dest);
		goto angles_phase;
	}

	move[0] = cam->dest[0] - ent->s.origin[0];
	move[1] = cam->dest[1] - ent->s.origin[1];
	move[2] = cam->dest[2] - ent->s.origin[2];
	dist = VectorNormalize(move);

	dt = level.time - cam->lasttime;
	if (dist < 0.0f)
		speed = -speed;
	time_to_close = (float)acos((double)(dist / speed));
	if (dt < time_to_close)
	{
		// clamp move scale = (2.0 * time_to_close - dt) * speed * dt
		dist = (speed * dt) * (2.0f * time_to_close - dt);
	}
	VectorScale(move, dist, move);
	move[0] += ent->s.origin[0];
	move[1] += ent->s.origin[1];
	move[2] += ent->s.origin[2];
	SubApplyCameraOrigin(ent, move);

angles_phase:
	if (cam->state == 0)
	{
		// target_ang = VectorNormalize2(viewtarget - dest)
		move[0] = cam->viewtarget[0] - cam->dest[0];
		move[1] = cam->viewtarget[1] - cam->dest[1];
		move[2] = cam->viewtarget[2] - cam->dest[2];
		VectorNormalize2(move, target_ang);
		// SubLerpAngle(cam->angles, target_ang, 0.2, level.time - lasttime)
		SubLerpAngle(cam->angles, target_ang, 0.2f, level.time - cam->lasttime);
	}
	else if (cam->state == 3)
	{
		// Aim at where the target is aiming: gclient_t.v_angle (at +0xe8c).
		SubLerpAngle(cam->angles,
		             cam->ent->client->v_angle,
		             0.8f,
		             level.time - cam->lasttime);
	}
	else if (cam->state == 7)
	{
		move[0] = cam->ent->s.origin[0] - ent->s.origin[0];
		move[1] = cam->ent->s.origin[1] - ent->s.origin[1];
		move[2] = cam->ent->s.origin[2] - ent->s.origin[2];
		VectorNormalize2(move, target_ang);
		SubLerpAngle(cam->angles, target_ang, 0.2f, level.time - cam->lasttime);
	}
	else
	{
		// Default (states 1, 2 and anything else): aim toward cam->ent's
		// origin and just normalise into cam->angles directly.
		move[0] = cam->ent->s.origin[0] - ent->s.origin[0];
		move[1] = cam->ent->s.origin[1] - ent->s.origin[1];
		move[2] = cam->ent->s.origin[2] - ent->s.origin[2];
		VectorNormalize2(move, cam->angles);
	}

	cmdangles[PITCH] = SHORT2ANGLE(ucmd->angles[PITCH]);
	cmdangles[YAW]   = SHORT2ANGLE(ucmd->angles[YAW]);
	cmdangles[ROLL]  = SHORT2ANGLE(ucmd->angles[ROLL]);
	SubApplyCameraAngles(ent, cam->angles, cmdangles);

	cam->lasttime = level.time;
} //end of the function CameraMove

//===========================================================================
// sub_10079f4d -- CameraAutoCamState1
//
// Just centerprints "fixed mode".
//===========================================================================
static void CameraAutoCamState1(edict_t *ent, usercmd_t *ucmd)
{
	gi.centerprintf(ent, "fixed mode");
} //end of the function CameraAutoCamState1

//===========================================================================
// sub_1007a1d2 -- CameraAutoCamState3
//
// Tracking state.  If target died, abort or transfer; otherwise verify line
// of sight, refresh muzzle / aim-end points, and time out after delay.
//===========================================================================
static void CameraAutoCamState3(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam = &ent->client->camera;

	if (cam->ent->deadflag != 0)
	{
		if (level.time < 31.0f)
			SubAbortAutocam(ent, ucmd);
		else
			SubOnTargetDeath(ent);
		return;
	}

	if (SubTargetVisible(ent, cam->ent))
	{
		SubGetTargetMuzzle(ent, cam->dest);
		SubGetTargetAimEnd(ent, cam->viewtarget);
		// gi.pointcontents(&cam->dest) & 1 == 1  OR  pause_time < level.time
		if ((gi.pointcontents(cam->dest) & 1)
		    || cam->pause_time < level.time)
		{
			SubSetAutocamTarget(ent, cam->ent, ucmd);
		}
	}
	else
	{
		SubSetAutocamTarget(ent, cam->ent, ucmd);
	}

	if (cam->search_time < level.time)
		SubAbortAutocam(ent, ucmd);
} //end of the function CameraAutoCamState3

//===========================================================================
// sub_10079f64 -- CameraAutoCamState2
//
// Idle-chase state.  Holds the camera at cam->dest while watching cam->ent.
// Promotes to state 3 when the target starts shooting at us / we have a
// good line of sight; otherwise re-targets when distance closes.
//===========================================================================
static void CameraAutoCamState2(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam = &ent->client->camera;
	vec3_t dir;
	vec3_t aimend;
	float dist, yaw_diff;

	if (cam->ent->deadflag != 0)
	{
		if (level.time < 31.0f)
			SubAbortAutocam(ent, ucmd);
		else
			SubOnTargetDeath(ent);
		return;
	}

	// If target is on ground (not == 'special-ignore-ent' global at 0x100cfd14),
	// fetch its aim point and decide whether to upgrade to tracking state 3.
	if (cam->ent->groundentity != NULL && cam->ent->groundentity != (edict_t *)NULL /* 0x100cfd14 */)
	{
		SubGetTargetMuzzle(ent, aimend);
		if (SubCanSeePoint(ent, aimend))
		{
			cam->state = 3;
			cam->pause_time = level.time + 10.0f;     // 0x10092168 = 10.0
			return;
		}
	}

	dir[0] = cam->dest[0] - cam->ent->s.origin[0];
	dir[1] = cam->dest[1] - cam->ent->s.origin[1];
	dir[2] = cam->dest[2] - cam->ent->s.origin[2];
	dist = VectorLength(dir);

	if (!SubTargetVisible(ent, cam->ent) || cam->maxflybydist > dist)
	{
		// Stale dest -- pick a fresh spot or new target.
		if (cam->pause_time < level.time)
		{
			SubAutocamSetSpot(cam->ent, cam->viewtarget);
		}
		else
		{
			SubSetAutocamTarget(ent, cam->ent, ucmd);
		}
	}

	dir[0] = cam->ent->s.origin[0] - ent->s.origin[0];
	dir[1] = cam->ent->s.origin[1] - ent->s.origin[1];
	dir[2] = cam->ent->s.origin[2] - ent->s.origin[2];
	dist = VectorLength(dir);

	if (dist < 170.0f)                                  // 0x100925e0 = 170
	{
		yaw_diff = (float)fabs((double)
		           (cam->ent->s.angles[YAW] - ent->s.angles[YAW]));
		if (yaw_diff > 180.0f)
			yaw_diff = 360.0f - yaw_diff;              // 0x10092150 = 360

		if (yaw_diff < 30.0f)                          // 0x1009217c = 30
		{
			SubGetTargetMuzzle(ent, aimend);
			if (SubCanSeePoint(ent, aimend))
			{
				cam->state = 3;
				cam->pause_time = level.time + 10.0f;
				return;
			}
		}
	}

	if (cam->search_time < level.time)
		SubAbortAutocam(ent, ucmd);
} //end of the function CameraAutoCamState2

//===========================================================================
// sub_10079c26 -- CameraAutoCamState7
//
// Bodyque (corpse) tracking.  When the chase target is a corpse it links
// to a chain via [+0x19c]; we follow that chain until it terminates or
// the corpse settles, then back the camera off by 59 units.
//===========================================================================
static void CameraAutoCamState7(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam = &ent->client->camera;
	vec3_t diff;
	float dist;
	float new_pause;

	if (cam->search_time < level.time)
	{
		SubAbortAutocam(ent, ucmd);
		return;
	}

	// If we've latched onto a 'bodyque' classname entity that has been
	// re-linked to a new corpse, follow the chain pointer.
	if (strcmp(cam->ent->classname, "bodyque") == 0
	    && cam->ent->deadflag == 0)
	{
		// edict_t.chain at +0x19c -- next corpse in the queue
		void *chain = cam->ent->chain;
		if (chain != NULL)
			cam->ent = chain;
		else
			{ SubAbortAutocam(ent, ucmd); return; }
	}

	if (cam->ent->inuse && cam->ent != ent)
	{
		// distance from camera to target; if close enough, extend pause.
		diff[0] = ent->s.origin[0] - cam->dest[0];
		diff[1] = ent->s.origin[1] - cam->dest[1];
		diff[2] = ent->s.origin[2] - cam->dest[2];
		dist = VectorLength(diff);
		if (dist <= 10.0f)
		{
			new_pause = level.time + 1.5f;            // 0x100922b0 = 1.5
			if (cam->pause_time < new_pause)
				cam->pause_time = new_pause;
		}
	}

	if (cam->pause_time < level.time)
	{
		SubAbortAutocam(ent, ucmd);
		return;
	}

	if (cam->ent->inuse && cam->ent != ent)
	{
		// Latch viewtarget onto the corpse origin (with z bump 8).
		cam->viewtarget[0] = cam->ent->s.origin[0];
		cam->viewtarget[1] = cam->ent->s.origin[1];
		cam->viewtarget[2] = cam->ent->s.origin[2] + 8.0f;   // 0x1009215c

		// If we can see straight to that point, no further work.
		if (!SubCanSeePoint(ent, cam->viewtarget))
		{
			// Else: if the corpse's velocity Y/Z are both <= 0,
			// abort -- it's stopped tumbling and we can't reach.
			float vy = cam->ent->velocity[1];
			float vz = cam->ent->velocity[2];
			if (vy <= 0.0f && vz <= 0.0f)
			{
				SubAbortAutocam(ent, ucmd);
				return;
			}
			SubSetAutocamTarget(ent, cam->ent, ucmd);
			cam->state = 7;
			cam->pause_time = level.time + 2.0f;          // 0x1009216c LSB = 2.0
		}

		// Refresh pause_time if velocity is non-zero (corpse still moving).
		{
			float vy = cam->ent->velocity[1];
			float vz = cam->ent->velocity[2];
			if (vy > 0.0f || vz > 0.0f)
				cam->pause_time = level.time + 2.0f;
		}
	}

	// Compute viewtarget = dest + 59 * normalize(dest - viewtarget)
	diff[0] = cam->dest[0] - cam->viewtarget[0];
	diff[1] = cam->dest[1] - cam->viewtarget[1];
	diff[2] = cam->dest[2] - cam->viewtarget[2];
	dist = VectorLength(diff);
	if (dist <= 60.0f)                                   // 0x10092230 = 60
		return;

	diff[0] = cam->dest[0] - cam->viewtarget[0];
	diff[1] = cam->dest[1] - cam->viewtarget[1];
	diff[2] = cam->dest[2] - cam->viewtarget[2];
	cam->dest[0] = diff[0];
	cam->dest[1] = diff[1];
	cam->dest[2] = diff[2];
	VectorNormalize(cam->dest);
	VectorMA(cam->viewtarget, 59.0f, cam->dest, cam->dest);   // 0x426c0000 = 59
} //end of the function CameraAutoCamState7

//===========================================================================
// sub_1007a2e7 -- CameraAutoCamState0
//
// Target-selection pass: walks the client list and picks a new chase target
// using one of three random strategies (highest health / highest score /
// uniform-random).  Then either commits to the new target via
// SubSetAutocamTarget, or -- if no different target was found -- refines
// cam->viewtarget and (when search_time has elapsed) backtracks cam->dest
// along the viewtarget->dest direction by a random distance.
//
// Reconstructed line-by-line from gamex86.dll @ 0x1007a2e7 (670 disasm
// lines).  The two trace-based "find a new dest2" blocks immediately after
// the angle-vectors call are dead in the original binary (their sqrt<=60
// guard is always satisfied because the operand is anglemod-bounded to
// [0,360)) -- preserved here as faithful no-ops via the same guard.
//===========================================================================
static void CameraAutoCamState0(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam = &ent->client->camera;
	edict_t  *chosen;
	edict_t  *it;
	float     best;
	float     rnd;
	float     angle_pitch;
	float     yaw_random;
	float     yaw_anglemod;
	float     yaw_to_target;
	vec3_t    angles;       // [-0x70..-0x68]: angles, later overwritten with fwd*2000
	vec3_t    delta;        // [-0x64..-0x5c]: target.origin - cam->dest, later angles via vectoangles
	vec3_t    fwd;          // [-0xc..0x0]: AngleVectors output
	vec3_t    diff;         // [-0x90..-0x88]: scratch for VectorLength
	vec3_t    new_dest;
	trace_t   tr;

	// ent->client->camera
	chosen = ent;

	// chosen := cam->lastent if alive
	if (cam->lastent && cam->lastent->deadflag == 0)
		chosen = cam->lastent;

	best = -1.0f;
	rnd  = (float)(rand() & 0x7FFF) / 32767.0f * 5.0f;   // 0x10092164 = 5.0

	// cam->goalent shortcut
	if (cam->goalent)
	{
		if (cam->goalent->deadflag == 0)
			chosen = cam->goalent;
		else
			chosen = ent;
		goto install_target;
	}

	if (rnd < 1.0f)                                       // 0x10092178 = 1.0
	{
		// Pick best-health client.
		for (it = SubNextClient(NULL); it != NULL; it = SubNextClient(it))
		{
			if (it != cam->lastent && it->deadflag == 0)
			{
				float h = (float)it->health;
				if (best < h)
				{
					chosen = it;
					best   = h;
				}
			}
		}
		goto install_target;
	}

	if (rnd < 2.0f)                                       // 0x1009216c = 2.0
	{
		// Pick best-score client (negative score clamped to zero).
		for (it = SubNextClient(NULL); it != NULL; it = SubNextClient(it))
		{
			if (it != cam->lastent && it->deadflag == 0)
			{
				float s = (float)it->client->resp.score;  // client + 0xda8
				if (s < 0.0f) s = 0.0f;
				if (best < s)
				{
					chosen = it;
					best   = s;
				}
			}
		}
		goto install_target;
	}

	// Uniform random pick: count valid candidates, then pick the n-th.
	best = 0.0f;
	for (it = SubNextClient(NULL); it != NULL; it = SubNextClient(it))
	{
		if (it != cam->lastent && it->deadflag == 0)
			best += 1.0f;                                 // 0x10092178 = 1.0
	}
	if (best <= 0.0f)
		goto install_target;

	best = (float)(rand() & 0x7FFF) / 32767.0f * best;

	it = NULL;
	for (;;)
	{
		it = SubNextClient(it);
		if (it == cam->lastent)         continue;
		if (it->deadflag != 0)          continue;
		best -= 1.0f;                                     // 0x10092178 = 1.0
		if (best > 0.0f)                continue;
		break;
	}
	if (it != NULL)
		chosen = it;

install_target:
	if (chosen != ent)
	{
		// Found a (different) target: commit and bail.
		SubSetAutocamTarget(ent, chosen, ucmd);
		cam->delay       = level.time + 10.0f;            // 0x10092168 = 10
		cam->search_time = level.time + 60.0f;            // 0x10092230 = 60
		return;
	}

	// No different target: tighten viewtarget by tracing from cam->dest
	// toward cam->viewtarget through walls/glass; the trace endpoint
	// becomes the new viewtarget.
	cam->ent = ent;
	tr = gi.trace(cam->dest, vec3_origin, vec3_origin,
	              cam->viewtarget, ent, OBSERVER_TRACE_MASK);
	cam->viewtarget[0] = tr.endpos[0];
	cam->viewtarget[1] = tr.endpos[1];
	cam->viewtarget[2] = tr.endpos[2];

	// Random pitch in [-20,+20] and random yaw in [0,360).
	angle_pitch = (float)(rand() & 0x7FFF) / 32767.0f * 40.0f - 20.0f;
	yaw_random  = (float)(rand() & 0x7FFF) / 32767.0f * 360.0f;
	angles[PITCH] = angle_pitch;
	angles[YAW]   = yaw_random;
	angles[ROLL]  = 0.0f;
	yaw_anglemod  = yaw_random;          // saved into [-0x74] before AngleVectors

	// If cam->dest already equals ent->s.origin, yaw_to_target stays 0;
	// otherwise compute yaw from cam->dest toward ent.
	if (VectorCompare(&ent->s.origin[0], cam->dest) == 0)
	{
		vec3_t out_ang;
		delta[0] = ent->s.origin[0] - cam->dest[0];
		delta[1] = ent->s.origin[1] - cam->dest[1];
		delta[2] = ent->s.origin[2] - cam->dest[2];
		vectoangles(delta, out_ang);
		delta[0] = out_ang[0];           // overwrite delta with the angles result
		delta[1] = out_ang[1];
		delta[2] = out_ang[2];
		yaw_to_target = delta[1];
	}
	else
	{
		yaw_to_target = 0.0f;
	}

	yaw_anglemod = anglemod(yaw_anglemod);

	// Convert the random pitch/yaw to a forward vector scaled by 2000.
	AngleVectors(angles, fwd, NULL, NULL);
	VectorScale(fwd, 2000.0f, angles);   // [-0x70..-0x68] reused as fwd*2000

	// best := |cam->dest2 - cam->dest|  (initial "best distance to beat").
	diff[0] = cam->dest2[0] - cam->dest[0];
	diff[1] = cam->dest2[1] - cam->dest[1];
	diff[2] = cam->dest2[2] - cam->dest[2];
	best = VectorLength(diff);

	// --- First trace candidate (dead code in the original) -----------------
	// Guard: sqrt(anglemod(yaw_anglemod - yaw_to_target)) > 60.0?  The
	// operand is in [0,360) so its sqrt is in [0,~19) and the comparison
	// always fails -- the block below never executes.  Kept for parity.
	if ((float)sqrt(anglemod(yaw_anglemod - yaw_to_target)) > 60.0)
	{
		new_dest[0] = delta[0] + cam->dest[0];
		new_dest[1] = delta[1] + cam->dest[1];
		new_dest[2] = delta[2] + cam->dest[2];
		tr = gi.trace(cam->dest, vec3_origin, vec3_origin,
		              new_dest, ent, OBSERVER_TRACE_MASK);
		delta[0] = tr.endpos[0];
		delta[1] = tr.endpos[1];
		delta[2] = tr.endpos[2];
		diff[0] = delta[0] - cam->dest[0];
		diff[1] = delta[1] - cam->dest[1];
		diff[2] = delta[2] - cam->dest[2];
		{
			float len = VectorLength(diff);
			if (best < len)
			{
				cam->dest2[0] = delta[0];
				cam->dest2[1] = delta[1];
				cam->dest2[2] = delta[2];
				best = len;
			}
		}
	}

	// --- Second trace candidate (yaw + 180) --------------------------------
	yaw_anglemod = anglemod(yaw_anglemod + 180.0f);       // 0x100922f0 = 180
	if ((float)sqrt(anglemod(yaw_anglemod - yaw_to_target)) > 60.0)
	{
		new_dest[0] = cam->dest[0] - angles[0];           // -fwd*2000
		new_dest[1] = cam->dest[1] - angles[1];
		new_dest[2] = cam->dest[2] - angles[2];
		tr = gi.trace(cam->dest, vec3_origin, vec3_origin,
		              new_dest, ent, OBSERVER_TRACE_MASK);
		delta[0] = tr.endpos[0];
		delta[1] = tr.endpos[1];
		delta[2] = tr.endpos[2];
		diff[0] = delta[0] - cam->dest[0];
		diff[1] = delta[1] - cam->dest[1];
		diff[2] = delta[2] - cam->dest[2];
		{
			float len = VectorLength(diff);
			if (best < len)
			{
				cam->dest2[0] = delta[0];
				cam->dest2[1] = delta[1];
				cam->dest2[2] = delta[2];
				best = len;
			}
		}
	}

	// pause_time elapsed: bump it [3, 5] seconds and resync viewtarget.
	if (cam->pause_time < level.time)
	{
		cam->pause_time = level.time + 3.0f               // 0x10092140 = 3
		                + 2.0f * ((float)(rand() & 0x7FFF) / 32767.0f);
		cam->viewtarget[0] = cam->dest2[0];
		cam->viewtarget[1] = cam->dest2[1];
		cam->viewtarget[2] = cam->dest2[2];
	}

	// search_time elapsed: backtrack along viewtarget->dest by a random
	// distance, and trace from ent toward that new point.
	if (cam->search_time < level.time)
	{
		float scale;

		diff[0] = cam->dest[0] - cam->viewtarget[0];
		diff[1] = cam->dest[1] - cam->viewtarget[1];
		diff[2] = cam->dest[2] - cam->viewtarget[2];
		VectorNormalize(diff);

		scale = (float)(rand() & 0x7FFF) / 32767.0f * 50.0f + 10.0f;   // [10,60]
		VectorScale(diff, scale, diff);

		diff[0] += cam->viewtarget[0];
		diff[1] += cam->viewtarget[1];
		diff[2] += cam->viewtarget[2];

		tr = gi.trace(ent->s.origin, vec3_origin, vec3_origin,
		              diff, ent, OBSERVER_TRACE_MASK);

		if (tr.fraction >= 1.0f)                          // 0x10092178 = 1.0
		{
			// Trace got all the way: commit the backtracked dest.
			cam->dest[0]  = diff[0];
			cam->dest[1]  = diff[1];
			cam->dest[2]  = diff[2];
			cam->dest2[0] = cam->viewtarget[0];
			cam->dest2[1] = cam->viewtarget[1];
			cam->dest2[2] = cam->viewtarget[2];
			cam->search_time = level.time + 8.0f          // 0x1009215c = 8
			                 + ((float)(rand() & 0x7FFF) / 32767.0f) * 5.0f;
		}
		else
		{
			// Trace blocked: pin dest2 to dest and retry soon.
			cam->dest2[0] = cam->dest[0];
			cam->dest2[1] = cam->dest[1];
			cam->dest2[2] = cam->dest[2];
			cam->search_time = level.time + 1.0f          // 0x10092178 = 1
			                 + ((float)(rand() & 0x7FFF) / 32767.0f);
		}
	}
} //end of the function CameraAutoCamState0

//===========================================================================
// Nested helper functions, reconstructed line-by-line from the gamex86.dll
// disassembly.  All helpers (including the fourth-tier sub_10078c53
// SubScoreCameraPos and sub_10078f4a SubCameraFindFlybyPos) are fully
// restored below.
//===========================================================================

// --- sub_10077d50 -----------------------------------------------------------
// Step `from` toward `to` by at most `maxstep` degrees, taking the shorter
// way around the circle.  Both inputs are anglemod'd first.  Returns the
// new angle, anglemod'd.
//===========================================================================
static float SubAngleStep(float from, float to, float maxstep)
{
	float diff;

	from = anglemod(from);
	to   = anglemod(to);

	if (from == to)
		return from;

	diff = to - from;
	if (to > from)
	{
		if (diff >  180.0) diff -= 360.0;
	}
	else
	{
		if (diff < -180.0) diff += 360.0;
	}

	if (diff > 0)
	{
		if (diff >  maxstep) diff =  maxstep;
	}
	else
	{
		if (diff < -maxstep) diff = -maxstep;
	}

	return anglemod(from + diff);
}

// --- sub_10077e29 -----------------------------------------------------------
// LerpAngles helper.  Steps pitch (out[0]) and yaw (out[1]) toward target,
// roll (out[2]) snaps directly.  Pitch uses a max of 100*frac*maxstep
// degrees, yaw uses 150*frac*maxstep --- the bot can pan faster than tilt.
// Note: also normalizes target[0] from (180,360] down to (-180,0].
//===========================================================================
static void SubLerpAngle(float *out, vec3_t target, float frac, float maxstep)
{
	float tmp;

	if (target[0] > 180.0f)
		target[0] -= 360.0f;

	tmp    = 100.0f * frac * maxstep;
	out[0] = SubAngleStep(out[0], target[0], tmp);

	tmp    = 150.0f * frac * maxstep;
	out[1] = SubAngleStep(out[1], target[1], tmp);

	out[2] = target[2];
}

// --- sub_10077eba is reconstructed as AngleDifference() at the top of this
// file (it is the public per gladq2_src/p_observer.h).  The earlier draft
// duplicated it as `SubAngleDifference`; that alias has been removed and the
// three call sites below in SubLerpAngle / SubApplyCameraAngles now call
// AngleDifference directly.
//===========================================================================

// --- sub_10077f15 -----------------------------------------------------------
// Apply a new origin to the observer.  Writes both pmove.origin (rounded to
// short) and s.origin (float).  Note: pmove.origin is written raw, not
// *8-scaled --- this is intentional, the observer's pmove is PM_FREEZE so
// pmove.origin is never read by the engine.
//===========================================================================
static void SubApplyCameraOrigin(edict_t *ent, vec3_t origin)
{
	gclient_t *cl = ent->client;
	cl->ps.pmove.origin[0] = (short)(int)origin[0];
	cl->ps.pmove.origin[1] = (short)(int)origin[1];
	cl->ps.pmove.origin[2] = (short)(int)origin[2];
	ent->s.origin[0] = origin[0];
	ent->s.origin[1] = origin[1];
	ent->s.origin[2] = origin[2];
}

// --- sub_10077f7d -----------------------------------------------------------
// Apply new view angles to the observer by locking the client's view via
// pmove.delta_angles.  delta_angles[i] = ANGLE2SHORT(new[i] - cmd[i]).
// Does NOT touch v_angle / ps.viewangles directly --- the engine will
// derive viewangles = cmd + delta on the next pmove tick.
//===========================================================================
static void SubApplyCameraAngles(edict_t *ent, vec3_t new_angles, vec3_t cmd_angles)
{
	gclient_t *cl = ent->client;
	float diff;
	int i;
	for (i = 0; i < 3; i++)
	{
		diff = AngleDifference(new_angles[i], cmd_angles[i]);
		diff = anglemod(diff);
		cl->ps.pmove.delta_angles[i] =
			(short)((int)(diff * 65536.0f / 360.0f) & 0xffff);
	}
}

// --- sub_10078125 -----------------------------------------------------------
// CanSee(ent, point): true if ent->s.origin can see `point` through the
// world (water boundary handled: if start/end straddle water, re-trace from
// the water-surface hit forward with the water content bits removed).
//===========================================================================
static qboolean SubCanSeePoint(edict_t *ent, vec3_t point)
{
	vec3_t start, end;
	int contmask = 1;
	trace_t tr;

	VectorCopy(ent->s.origin, start);

	if (!gi.inPVS(start, point))
		return false;

	if (gi.pointcontents(point) & MASK_WATER)
		contmask |= MASK_WATER;

	if (gi.pointcontents(start) & MASK_WATER)
	{
		if (!(contmask & MASK_WATER))
		{
			VectorCopy(point, start);
			VectorCopy(ent->s.origin, end);
		}
		else
		{
			VectorCopy(start, end);
			end[0] = point[0]; end[1] = point[1]; end[2] = point[2];
		}
		contmask ^= MASK_WATER;
	}
	else
	{
		VectorCopy(point, end);
	}

	tr = gi.trace(start, NULL, NULL, end, ent, contmask);

	if (tr.contents & MASK_WATER)
	{
		if (tr.surface == NULL || !(tr.surface->flags & (SURF_TRANS33|SURF_TRANS66)))
		{
			contmask &= ~MASK_WATER;
			tr = gi.trace(tr.endpos, NULL, NULL, end, ent, contmask);
		}
	}

	return tr.fraction >= 1.0f;
}

// --- sub_100782ad -----------------------------------------------------------
// TargetVisible(ent, target): like SubCanSeePoint, but trace endpoint is
// target->s.origin and target itself is treated as a valid hit
// (tr.ent == target counts as visible).
//===========================================================================
static qboolean SubTargetVisible(edict_t *ent, edict_t *target)
{
	vec3_t start, end;
	edict_t *passent = ent;
	int contmask = 1;
	trace_t tr;

	if (!gi.inPVS(ent->s.origin, target->s.origin))
		return false;

	VectorCopy(ent->s.origin,    start);
	VectorCopy(target->s.origin, end);

	if (gi.pointcontents(target->s.origin) & MASK_WATER)
		contmask |= MASK_WATER;

	if (gi.pointcontents(ent->s.origin) & MASK_WATER)
	{
		if (!(contmask & MASK_WATER))
		{
			passent = target;
			VectorCopy(target->s.origin, start);
			VectorCopy(ent->s.origin,    end);
		}
		contmask ^= MASK_WATER;
	}

	tr = gi.trace(start, NULL, NULL, end, passent, contmask);

	if (tr.contents & MASK_WATER)
	{
		if (tr.surface == NULL || !(tr.surface->flags & (SURF_TRANS33|SURF_TRANS66)))
		{
			contmask &= ~MASK_WATER;
			tr = gi.trace(tr.endpos, NULL, NULL, end, passent, contmask);
		}
	}

	return (tr.fraction >= 1.0f) || (tr.ent == target);
}

// --- sub_1007845f -----------------------------------------------------------
// NextClient(prev): return next entity after `prev` in g_edicts that has
// classname=="player" (case-insensitive), is inuse, and does NOT have the
// FL_OBSERVER (0x10000) flag.  If `prev` is NULL, start from g_edicts[1]
// (the lea +0x458 indexes prev+1 in either case).
//===========================================================================
static edict_t *SubNextClient(edict_t *prev)
{
	int i;
	int num_edicts = globals.num_edicts;

	if (prev == NULL)
		i = 0;
	else
		i = (int)(prev - g_edicts);

	for (; i < num_edicts; i++)
	{
		edict_t *e = &g_edicts[i + 1];
		if (!e->inuse)
			continue;
		if (e->flags & FL_OBSERVER)
			continue;
		if (Q_stricmp(e->classname, "player") == 0)
			return e;
	}
	return NULL;
}

// --- sub_1007806c -----------------------------------------------------------
// SubCenterPrintScore: when the OBSERVER's camera-state flag 0x10
// (CAMFL_NAME == "show target name + score") is set in
// client->camera.flags (offset client+0xf98), print the target's name
// and current score to the observer via gi.centerprintf.  Used by
// SubOnTargetDeath and SubSetAutocamTarget as a HUD notify.
//
// Reconstructed line-by-line from gamex86.dll @ 0x1007806c.  Stack
// frame: char score_buf[0x80] @ [ebp-0x80], char fragword[0x80] @
// [ebp-0x100].  The CRT helpers at 0x1008721f and 0x10087010 are
// sprintf() and strcpy() respectively.
//===========================================================================
static void SubCenterPrintScore(edict_t *ent, edict_t *target, char *prefix)
{
	char score_buf[0x80];                                /* [ebp-0x80]  */
	char fragword[0x80];                                 /* [ebp-0x100] */
	int  score;

	if (!(ent->client->camera.flags & CAMFL_NAME))      /* +0xf98 */
		return;

	score = target->client->resp.score;                 /* +0xda8 */
	sprintf(score_buf, "%d", score);

	if (score == 1 || score == -11)
		strcpy(fragword, "frag");
	else
		strcpy(fragword, "frags");

	gi.centerprintf(ent, "%s\n\n\n%s - %s %s",
	                prefix,
	                target->client->pers.netname,       /* +0x2bc */
	                score_buf,
	                fragword);
}

// --- sub_1007886c -----------------------------------------------------------
// SubCameraSnapToOrigin: snap the autocam's dest to the observer's own
// origin, derive a viewtarget one unit forward along the current
// cam->angles, then enter idle (state 0) via CameraMove(ent, 0, ucmd).
// Used by SubAbortAutocam.
//
// Reconstructed line-by-line from gamex86.dll @ 0x1007886c.  The
// compiler kept cam at [ebp-0x10] = &client->camera (client+0xf64) and
// the AngleVectors out-vector at [ebp-0xc..-4].  Field offsets used:
//   cam+0x04 = angles, cam+0x40 = dest, cam+0x4c = viewtarget
//   ent+0x04 = ent->s.origin
//===========================================================================
static void SubCameraSnapToOrigin(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam;
	vec3_t forward;                                       /* [ebp-0xc..-4] */

	cam = &ent->client->camera;                           /* [ebp-0x10]    */

	cam->dest[0] = ent->s.origin[0];
	cam->dest[1] = ent->s.origin[1];
	cam->dest[2] = ent->s.origin[2];

	AngleVectors(cam->angles, forward, NULL, NULL);

	cam->viewtarget[0] = forward[0] + ent->s.origin[0];
	cam->viewtarget[1] = forward[1] + ent->s.origin[1];
	cam->viewtarget[2] = forward[2] + ent->s.origin[2];

	CameraMove(ent, 0, ucmd);
}

// --- sub_100788ff -----------------------------------------------------------
// AbortAutocam: stop following any target, lock the current view in place,
// hold for 2 seconds.
//===========================================================================
static void SubAbortAutocam(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam = &ent->client->camera;

	SubCameraSnapToOrigin(ent, ucmd);

	cam->state       = 0;
	cam->pause_time  = level.time + 2.0f;
	cam->search_time = level.time;
	cam->dest2[0]    = cam->viewtarget[0];
	cam->dest2[1]    = cam->viewtarget[1];
	cam->dest2[2]    = cam->viewtarget[2];
	cam->ent         = ent;
}

// --- sub_1007897a -----------------------------------------------------------
// OnTargetDeath: announce score, then drop into bodyque-chase (state 7)
// for 2 seconds with a 5-second search timeout.
//===========================================================================
static void SubOnTargetDeath(edict_t *ent)
{
	camera_t *cam = &ent->client->camera;

	SubCenterPrintScore(ent, cam->ent, "");

	cam->dest[0]     = ent->s.origin[0];
	cam->dest[1]     = ent->s.origin[1];
	cam->dest[2]     = ent->s.origin[2];
	cam->state       = 7;
	cam->pause_time  = level.time + 2.0f;
	cam->lastent     = NULL;
	cam->search_time = level.time + 5.0f;
}

// --- sub_10078a04 -----------------------------------------------------------
// GetTargetMuzzle: compute a "muzzle"-aligned camera offset and trace from
// the target's viewpoint along it.  Pitch is decoupled (camera stays
// horizontal); 90 units back and 16 units up.  If the trace hits a wall,
// snap to 70 units off the wall along the wall normal; otherwise, push 20
// more units backward beyond the offset point.
//===========================================================================
static void SubGetTargetMuzzle(edict_t *ent, vec3_t out)
{
	camera_t *cam = &ent->client->camera;
	vec3_t angles, up, fwd, viewfrom, endpos, ofs;
	float adj;
	trace_t tr;

	/* angles = cam->angles, but pitch zeroed, yaw anglemod'd */
	angles[0] = 0.0f;
	angles[1] = anglemod(cam->angles[1] + 0.0f);
	angles[2] = cam->angles[2];

	/* 1) up from full angles */
	AngleVectors(cam->angles, NULL, NULL, up);

	/* 2) forward from flat angles */
	AngleVectors(angles, fwd, NULL, NULL);

	/* re-tilt forward so it's perpendicular to up */
	adj = (fwd[0]*up[0] + fwd[1]*up[1]) * -1.0f / up[2];
	fwd[2] = adj;
	VectorNormalize(fwd);

	/* ofs = -90 * fwd + (0,0,16) */
	VectorScale(fwd, -90.0f, ofs);
	ofs[2] += 16.0f;

	/* viewfrom = cam->ent->s.origin + cam->ent->client->ps.viewoffset */
	{
		gclient_t *tc = cam->ent->client;
		viewfrom[0] = cam->ent->s.origin[0] + tc->ps.viewoffset[0];   /* +0x28 */
		viewfrom[1] = cam->ent->s.origin[1] + tc->ps.viewoffset[1];   /* +0x2c */
		viewfrom[2] = cam->ent->s.origin[2] + tc->ps.viewoffset[2];   /* +0x30 */
	}

	endpos[0] = viewfrom[0] + ofs[0];
	endpos[1] = viewfrom[1] + ofs[1];
	endpos[2] = viewfrom[2] + ofs[2];

	tr = gi.trace(viewfrom, NULL, NULL, endpos, cam->ent, OBSERVER_TRACE_MASK);

	if (tr.fraction < 1.0f && tr.ent != g_edicts)
		VectorMA(tr.endpos, 70.0f, tr.plane.normal, out);
	else
		VectorMA(tr.endpos, 20.0f, fwd, out);
}

// --- sub_10078bcc -----------------------------------------------------------
// GetTargetAimEnd: 2048 units along cam->angles from cam->ent->s.origin
// when the target is alive; just the corpse position when dead.
//===========================================================================
static void SubGetTargetAimEnd(edict_t *ent, vec3_t out)
{
	camera_t *cam = &ent->client->camera;

	if (cam->ent->deadflag == 0)
	{
		vec3_t fwd;
		AngleVectors(cam->angles, fwd, NULL, NULL);
		VectorMA(cam->ent->s.origin, 2048.0f, fwd, out);
	}
	else
	{
		out[0] = cam->ent->s.origin[0];
		out[1] = cam->ent->s.origin[1];
		out[2] = cam->ent->s.origin[2];
	}
}

// --- sub_10079a92 -----------------------------------------------------------
// AutocamSetSpot: copy `src->s.origin` to `spot`, then override the Z to
// (src->maxs[2] - 8) --- a point near the top of the entity's bounding box.
//===========================================================================
static void SubAutocamSetSpot(edict_t *src, vec3_t spot)
{
	spot[0] = src->s.origin[0];
	spot[1] = src->s.origin[1];
	spot[2] = src->s.origin[2];
	spot[2] = src->maxs[2] - 8.0f;
}

// --- sub_10079acf -----------------------------------------------------------
// SetAutocamTarget: bind the camera to a new target, compute a good vantage
// point via the flyby helper SubCameraFindFlybyPos, and either commit to
// state 2 (flyby) or fall back to abort.
//===========================================================================
static qboolean SubCameraFindFlybyPos(edict_t *ent, edict_t *target, vec3_t out_pos); /* sub_10078f4a */

static void SubSetAutocamTarget(edict_t *ent, edict_t *target, usercmd_t *ucmd)
{
	camera_t *cam = &ent->client->camera;
	vec3_t   pos, diff;
	float    len;

	cam->pause_time = level.time + 0.4f;

	if (cam->ent != target)
	{
		cam->ent = target;
		if (cam->lastent != cam->ent)
		{
			SubCenterPrintScore(ent, cam->ent, "looking at");
			cam->lastent = target;
		}
	}

	if (!SubCameraFindFlybyPos(ent, target, pos))
	{
		SubAbortAutocam(ent, ucmd);
		cam->pause_time = level.time + 2.0f;
		return;
	}

	SubAutocamSetSpot(target, cam->viewtarget);

	cam->dest[0] = pos[0];
	cam->dest[1] = pos[1];
	cam->dest[2] = pos[2];
	cam->state   = 2;

	diff[0] = cam->dest[0] - cam->viewtarget[0];
	diff[1] = cam->dest[1] - cam->viewtarget[1];
	diff[2] = cam->dest[2] - cam->viewtarget[2];

	len = VectorLength(diff) * 1.5f;
	if (len > 500.0f) len = 500.0f;
	cam->maxflybydist = len;

	CameraMove(ent, 0, ucmd);
}

// NOTE: sub_10085904 in the shipping DLL is the q_shared.c VectorCompare
// statically linked into the .text segment.  We do not re-define it
// locally; callers below use the shared declaration from q_shared.h.

// --- sub_10078c53 -----------------------------------------------------------
// Score a candidate camera offset relative to the target (cam->ent).
//
//   * Normalises 'ofs' in place, then scales it to 700 units (the search
//     radius).  Callers that share an offset vector across multiple calls
//     will therefore see it overwritten.
//   * Traces 700 units from the target's bbox-top (origin with z = maxs[2]-8)
//     along 'ofs' with mask 0x2010003 (MASK_SHOT|MASK_OPAQUE).  If the trace
//     hits anything other than worldspawn the candidate is rejected.
//   * The candidate point is the trace endpos pulled 5 units back toward
//     the target.  If that point is inside solid (pointcontents & 1) the
//     candidate is rejected.
//   * If the target and candidate are on opposite sides of a water surface
//     (mask 0x38 = CONTENTS_LAVA|SLIME|WATER) re-trace with the water bits
//     included so the candidate is moved to the water boundary.
//   * If the resulting trace contents include water, require that we hit a
//     translucent surface (SURF_TRANS33|SURF_TRANS66 = 0x30); otherwise
//     reject.
//   * Reject anything farther than 50 units from the target.
//   * Returns 1111 for any rejection (well above the 1000-init); otherwise
//     returns sqrt(333 - dist).  Lower scores correspond to *larger* (but
//     <= 50) distances from the target, so callers minimise.
//===========================================================================
static float SubScoreCameraPos(edict_t *ent, vec3_t ofs, vec3_t out_endpos)
{
	camera_t *cam = &ent->client->camera;
	vec3_t    mins, maxs;
	vec3_t    unit5;            /* [ebp - 0x6c] = unit_ofs * 5 */
	vec3_t    viewfrom;         /* [ebp - 0x78] */
	vec3_t    end;              /* [ebp - 0x50] */
	vec3_t    diff;             /* [ebp - 0x9c] */
	trace_t   tr;               /* [ebp - 0xd4] / copied to [ebp - 0x38] */
	int       cv_view, cv_end;
	float     dist;

	mins[0] = mins[1] = mins[2] = -8.0f;
	maxs[0] = maxs[1] = maxs[2] =  8.0f;

	VectorNormalize(ofs);
	VectorScale(ofs, 5.0f,   unit5);
	VectorScale(ofs, 700.0f, ofs);

	VectorCopy(cam->ent->s.origin, viewfrom);
	viewfrom[2] = cam->ent->absmax[2] - 8.0f;

	end[0] = viewfrom[0] + ofs[0];
	end[1] = viewfrom[1] + ofs[1];
	end[2] = viewfrom[2] + ofs[2];

	tr = gi.trace(viewfrom, mins, maxs, end, cam->ent, MASK_SHOT|MASK_OPAQUE);

	if (tr.ent != g_edicts)
		return 1111.0f;

	out_endpos[0] = tr.endpos[0] - unit5[0];
	out_endpos[1] = tr.endpos[1] - unit5[1];
	out_endpos[2] = tr.endpos[2] - unit5[2];

	if (gi.pointcontents(out_endpos) & 1)
		return 1111.0f;

	cv_view = gi.pointcontents(viewfrom) & MASK_WATER;
	cv_end  = gi.pointcontents(out_endpos) & MASK_WATER;

	if (cv_view && !cv_end)
	{
		vec3_t start2;
		start2[0] = tr.endpos[0];
		start2[1] = tr.endpos[1];
		start2[2] = tr.endpos[2];
		tr = gi.trace(start2, NULL, NULL, viewfrom, cam->ent,
		              MASK_SHOT|MASK_OPAQUE|MASK_WATER);
	}
	else if (!cv_view && cv_end)
	{
		vec3_t start2;
		start2[0] = tr.endpos[0];
		start2[1] = tr.endpos[1];
		start2[2] = tr.endpos[2];
		tr = gi.trace(viewfrom, NULL, NULL, start2, cam->ent,
		              MASK_SHOT|MASK_OPAQUE|MASK_WATER);
	}

	if (tr.contents & MASK_WATER)
	{
		if (!tr.surface)
			return 1111.0f;
		if (!(tr.surface->flags & (SURF_TRANS33|SURF_TRANS66)))
			return 1111.0f;
	}

	diff[0] = cam->ent->s.origin[0] - out_endpos[0];
	diff[1] = cam->ent->s.origin[1] - out_endpos[1];
	diff[2] = cam->ent->s.origin[2] - out_endpos[2];
	dist = VectorLength(diff);
	if (dist > 50.0f)
		return 1111.0f;

	return (float)sqrt(333.0 - (double)dist);
}

// --- sub_10078f4a -----------------------------------------------------------
// CameraFindFlybyPos: 6-tier search for a vantage point around the current
// camera target (cam->ent).  Each tier tries 4 candidate offset directions
// (combinations of forward/right/up basis vectors from the camera's
// cam->angles) and keeps the best (lowest score from SubScoreCameraPos).  If
// any tier finds a valid candidate (best < 1000) the remaining tiers are
// skipped.  Returns 1 and writes the chosen world position to 'out' on
// success; returns 0 if no tier found a position.
//
// The 'target' arg is present in the original signature (the caller in
// SubSetAutocamTarget passes it after assigning cam->ent = target) but is
// never read -- the disassembly only ever dereferences ent and the cam->ent
// it points at.
//
// Side-effect to be aware of: starting at tier 3 the helper passes the
// running forward/right basis vectors *directly* to SubScoreCameraPos, which
// normalises and re-scales them to 700 units.  Tiers 4-5 then use those
// post-scoring values when building their additive offsets.
//===========================================================================
#define TRY_CANDIDATE(ofs_expr) do { \
		ofs_expr; \
		score = SubScoreCameraPos(ent, cand_ofs, cand_endpos); \
		if (score < best_score) { \
			best_score = score; \
			VectorCopy(cand_endpos, best_endpos); \
		} \
	} while (0)

static qboolean SubCameraFindFlybyPos(edict_t *ent, edict_t *target, vec3_t out)
{
	camera_t *cam = &ent->client->camera;
	vec3_t    fwd, right, up;
	vec3_t    cand_ofs;
	vec3_t    cand_endpos;
	vec3_t    best_endpos;
	float     best_score;
	float     score;

	(void)target;   /* unused in the original */

	best_endpos[0] = 0;
	best_endpos[1] = 0;
	best_endpos[2] = cam->ent->s.angles[2]; /* raw seed from [ent+0x18] */

	AngleVectors(cam->angles, fwd, right, up);
	VectorScale(fwd, 3.0f, fwd);
	best_score = 1000.0f;

	/* Tier 0: ±3fwd ±right combined with +up */
	TRY_CANDIDATE((VectorAdd     (up, fwd, cand_ofs),
	               VectorAdd     (cand_ofs, right, cand_ofs)));
	TRY_CANDIDATE((VectorSubtract(up, fwd, cand_ofs),
	               VectorAdd     (cand_ofs, right, cand_ofs)));
	TRY_CANDIDATE((VectorAdd     (up, fwd, cand_ofs),
	               VectorSubtract(cand_ofs, right, cand_ofs)));
	TRY_CANDIDATE((VectorSubtract(up, fwd, cand_ofs),
	               VectorSubtract(cand_ofs, right, cand_ofs)));

	if (best_score >= 1000.0f)
	{
		/* Tier 1: ±3fwd alone or ±right alone, combined with +up */
		TRY_CANDIDATE( VectorAdd     (up, fwd,   cand_ofs));
		TRY_CANDIDATE( VectorSubtract(up, fwd,   cand_ofs));
		TRY_CANDIDATE( VectorAdd     (up, right, cand_ofs));
		TRY_CANDIDATE( VectorSubtract(up, right, cand_ofs));
	}

	if (best_score >= 1000.0f)
	{
		/* Tier 2: ±3fwd ±right at target altitude */
		TRY_CANDIDATE( VectorAdd     (fwd,   right, cand_ofs));
		TRY_CANDIDATE( VectorSubtract(fwd,   right, cand_ofs));
		TRY_CANDIDATE( VectorSubtract(right, fwd,   cand_ofs));
		TRY_CANDIDATE((VectorClear   (cand_ofs),
		               VectorSubtract(cand_ofs, fwd,   cand_ofs),
		               VectorSubtract(cand_ofs, right, cand_ofs)));
	}

	if (best_score >= 1000.0f)
	{
		/* Tier 3: pure ±fwd or ±right (these calls mutate fwd/right to 700*unit) */
		score = SubScoreCameraPos(ent, fwd,   cand_endpos);
		if (score < best_score) { best_score = score; VectorCopy(cand_endpos, best_endpos); }
		score = SubScoreCameraPos(ent, right, cand_endpos);
		if (score < best_score) { best_score = score; VectorCopy(cand_endpos, best_endpos); }
		TRY_CANDIDATE((VectorClear(cand_ofs), VectorSubtract(cand_ofs, fwd,   cand_ofs)));
		TRY_CANDIDATE((VectorClear(cand_ofs), VectorSubtract(cand_ofs, right, cand_ofs)));
	}

	if (best_score >= 1000.0f)
	{
		/* Tier 4: -up combined with ±fwd ±right */
		TRY_CANDIDATE((VectorClear(cand_ofs),
		               VectorSubtract(cand_ofs, up,    cand_ofs),
		               VectorAdd     (cand_ofs, fwd,   cand_ofs),
		               VectorAdd     (cand_ofs, right, cand_ofs)));
		TRY_CANDIDATE((VectorClear(cand_ofs),
		               VectorSubtract(cand_ofs, up,    cand_ofs),
		               VectorSubtract(cand_ofs, fwd,   cand_ofs),
		               VectorAdd     (cand_ofs, right, cand_ofs)));
		TRY_CANDIDATE((VectorClear(cand_ofs),
		               VectorSubtract(cand_ofs, up,    cand_ofs),
		               VectorAdd     (cand_ofs, fwd,   cand_ofs),
		               VectorSubtract(cand_ofs, right, cand_ofs)));
		TRY_CANDIDATE((VectorClear(cand_ofs),
		               VectorSubtract(cand_ofs, up,    cand_ofs),
		               VectorSubtract(cand_ofs, fwd,   cand_ofs),
		               VectorSubtract(cand_ofs, right, cand_ofs)));
	}

	if (best_score >= 1000.0f)
	{
		/* Tier 5: -up combined with ±fwd alone or ±right alone */
		TRY_CANDIDATE((VectorClear(cand_ofs),
		               VectorSubtract(cand_ofs, up,  cand_ofs),
		               VectorAdd     (cand_ofs, fwd, cand_ofs)));
		TRY_CANDIDATE((VectorClear(cand_ofs),
		               VectorSubtract(cand_ofs, up,    cand_ofs),
		               VectorAdd     (cand_ofs, right, cand_ofs)));
		TRY_CANDIDATE((VectorClear(cand_ofs),
		               VectorSubtract(cand_ofs, up,  cand_ofs),
		               VectorSubtract(cand_ofs, fwd, cand_ofs)));
		TRY_CANDIDATE((VectorClear(cand_ofs),
		               VectorSubtract(cand_ofs, up,    cand_ofs),
		               VectorSubtract(cand_ofs, right, cand_ofs)));
	}

	if (best_score >= 1000.0f)
		return false;

	VectorCopy(best_endpos, out);
	return true;
}
#undef TRY_CANDIDATE

//===========================================================================
// DoObserver                                                 sub_1007b926
//
// Called from ClientThink per frame.  Returns 0 if the entity is not an
// observer (caller continues normal player think).  When observing, runs
// the autocam/chasecam jump-button retarget, dispatches to
// CameraAutoCamThink / CameraChaseCamThink, and either zeros the usercmd
// for a spectator frame or returns 1 with PM_SPECTATOR set when the
// camera has been handed back to the observer himself.
//===========================================================================
int DoObserver(edict_t *ent, usercmd_t *ucmd)
{
	camera_t *cam;
	edict_t *next;

	if (!(ent->flags & FL_OBSERVER)) return 0;
	if (!ent->client) return 0;

	cam = &ent->client->camera;
	if (!(cam->flags & CAMFL_AUTOCAM))
		ClientPlaceCamera(ent);

	// jump button (upmove > 100) cycles the target / chase camera, with a
	// 0.7s debounce held in cam->lastcycle so a held key doesn't tear
	// through the client list in one frame
	if (ucmd->upmove > 100 && level.time - 0.7 > cam->lastcycle)
	{
		cam->lastcycle = level.time;
		if (cam->flags & CAMFL_AUTOCAM)
		{
			next = SubNextClient(cam->ent);
			if (!next || next == ent)
				next = SubNextClient(next);
			if (next && next != ent)
				SubSetAutocamTarget(ent, next, ucmd);
		} //end if
		else if (cam->flags & CAMFL_CHASECAM)
		{
			ClientCycleCamera(ent);
		} //end else if
	} //end if

	ent->client->ps.gunindex = 0;

	if ((cam->flags & (CAMFL_AUTOCAM | CAMFL_CHASECAM)) &&
	    !(cam->ent == ent && (cam->flags & CAMFL_CHASECAM)))
	{
		// tracking some other client: drive the camera and silence input
		ent->client->ps.pmove.pm_type = PM_DEAD;
		if (cam->flags & CAMFL_AUTOCAM)
			CameraAutoCamThink(ent, ucmd);
		else if (cam->flags & CAMFL_CHASECAM)
			CameraChaseCamThink(ent, ucmd);
		ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_USE);
		ucmd->forwardmove = 0;
		ucmd->sidemove = 0;
		ucmd->upmove = 0;
		ent->client->ps.pmove.gravity = 0;
		return 1;
	} //end if
	else
	{
		// plain spectator (no autocam/chasecam, or chasecam targeting self)
		ent->movetype = MOVETYPE_NOCLIP;
		ent->client->ps.pmove.pm_type = PM_SPECTATOR;
		ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_USE);
		return 1;
	} //end else
} //end of the function DoObserver

//===========================================================================
// ClientObserverCmd
//
// returns true if cmd was an observer subcommand and was handled.
//===========================================================================
qboolean ClientObserverCmd(char *cmd, edict_t *ent)
{
	if (!Q_stricmp(cmd, "observer"))     { ClientToggleObserver(ent);    return true; }
	if (!Q_stricmp(cmd, "autocam"))      { ClientToggleAutoCam(ent);     return true; }
	if (!Q_stricmp(cmd, "chasecam"))     { ClientToggleChaseCam(ent);    return true; }
	if (!Q_stricmp(cmd, "cyclecam"))     { ClientCycleCamera(ent);       return true; }
	if (!Q_stricmp(cmd, "setcam"))       { ClientSetCamera(ent);         return true; }
	if (!Q_stricmp(cmd, "camfixed"))     { ClientToggleCameraFixed(ent); return true; }
	if (!Q_stricmp(cmd, "camname"))      { ClientToggleCameraName(ent);  return true; }
	if (!Q_stricmp(cmd, "observerhelp")) { ClientObserverHelp(ent);      return true; }
	return false;
} //end of the function ClientObserverCmd

#endif //OBSERVER
