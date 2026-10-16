/* The spider's body and legs: pure maths, no X or Cairo in here.
 * Units: pixels for positions; one "body unit" u = 5 px * size. */
#include "spidereko.h"
#include <math.h>
#include <string.h>

#define WAKE_DIST       12.0
#define HIP_SIDE_OFFSET 0.55

static const double LEG_HX[4]  = {0.90, 0.35, -0.25, -0.85};
static const double LEG_ANG[4] = {35.0, 75.0, 112.0, 150.0};

static double wrap_angle(double a)
{
    a = fmod(a + M_PI, 2 * M_PI);
    if (a < 0) a += 2 * M_PI;
    return a - M_PI;
}

static double smoothstep(double t) { return t * t * (3 - 2 * t); }
static cplx polar(double r, double ang) { return r * (cos(ang) + I * sin(ang)); }

static void apply_size(Sim *s)
{
    double u = s->u = 5.0 * s->cfg->size;
    s->L1 = 3.0 * u;                     /* thigh */
    s->L2 = 4.4 * u;                     /* shin  */
    s->rest_radius = 5.2 * u;
    s->max_reach = (s->L1 + s->L2) * 0.995;
    s->arrive_r = 0.6 * u;
    s->turn_min = 2.0 * u;               /* closer than this to its spot it stops steering towards it */
    s->look_min = 2.0 * u;               /* pointer closer than this: keep the heading (no spinning)   */
    s->sit_dist = (6.6 + 1.2) * u;       /* body centre -> pointer when sitting just above it          */
    s->window_size = (int)(19 * u) + 8;
    for (int i = 0; i < 8; i++) s->legs[i].hx = LEG_HX[i % 4] * u;
}

static void place_hips(Sim *s)
{
    cplx f = polar(1, s->heading), n = f * I;
    for (int i = 0; i < 8; i++) {
        Leg *l = &s->legs[i];
        l->hip = s->pos + f * l->hx + n * (l->side * HIP_SIDE_OFFSET * s->u);
        l->rest = l->hip + polar(s->rest_radius * s->curl, s->heading + l->side * l->angle);
    }
}

void sim_init(Sim *s, const Cfg *cfg, cplx start)
{
    memset(s, 0, sizeof *s);
    s->cfg = cfg;
    s->pos = start;
    s->heading = M_PI;
    s->curl = 1.0;
    s->state = CHASE;
    for (int i = 0; i < 8; i++) {
        Leg *l = &s->legs[i];
        int side = i < 4 ? -1 : 1, j = i % 4;
        l->side = side;
        l->angle = LEG_ANG[j] * M_PI / 180.0;
        l->group = (j + (side < 0 ? 0 : 1)) % 2;
        l->dur = 0.1;
    }
    apply_size(s);
    place_hips(s);
    for (int i = 0; i < 8; i++) {
        Leg *l = &s->legs[i];
        l->foot = l->src = l->dst = l->rest;
    }
}

void sim_set_size(Sim *s)
{
    apply_size(s);
    place_hips(s);
    for (int i = 0; i < 8; i++) {
        Leg *l = &s->legs[i];
        l->foot = l->src = l->dst = l->rest;
        l->stepping = 0;
        l->t = 0;
    }
}

void sim_wake(Sim *s)
{
    if (s->state == SLEEP) {
        s->state = CHASE;
        s->idle_t = 0;
    }
}

cplx sim_anchor_for(const Sim *s, cplx look)
{
    return look - I * s->sit_dist * (1.0 + 2.0 * s->cfg->lift / 100.0);
}

void sim_leg_joints(const Sim *s, const Leg *leg, cplx *hip_o, cplx *knee_o, cplx *foot_o)
{
    cplx hip = leg->hip, v = leg->foot - hip;
    double d = cabs(v);
    double lo = fabs(s->L1 - s->L2) + 1e-3, hi = s->max_reach;
    if (d < 1e-6) { v = polar(1, s->heading); d = 1.0; }
    v = v / d * fmin(fmax(d, lo), hi);
    d = cabs(v);
    double a = (s->L1 * s->L1 - s->L2 * s->L2 + d * d) / (2 * d);
    double h = sqrt(fmax(s->L1 * s->L1 - a * a, 0.0));
    if (leg->stepping) h *= 1.0 + 0.35 * sin(M_PI * leg->t);       /* the knee lifts while stepping */
    cplx dir = v / d, base = hip + dir * a;
    cplx k1 = base + dir * I * h, k2 = base - dir * I * h;
    *hip_o = hip;
    *knee_o = cabs(k1 - s->pos) > cabs(k2 - s->pos) ? k1 : k2;      /* the knee bends away from the body */
    *foot_o = hip + v;
}

static cplx landing(const Sim *s, const Leg *l, double remaining, double trigger, cplx forward)
{
    cplx land = l->rest + s->vel * remaining + forward * trigger * 0.6;
    cplx future_hip = l->hip + s->vel * remaining;
    cplx off = land - future_hip;
    double limit = s->max_reach * 0.92;
    if (cabs(off) > limit) land = future_hip + off / cabs(off) * limit;
    return land;
}

static void walk_legs(Sim *s, double dt, double speed)
{
    int moving = speed > 30.0;
    double radius = s->rest_radius;
    double trigger = radius * (moving ? 0.45 : 0.12);
    double dur = s->state == CHASE ? fmax(0.045, fmin(0.12, 0.9 * radius / fmax(speed, 1.0))) : 0.25;
    cplx forward = moving ? s->vel / speed : 0;
    int airborne = 0;                               /* bit g set = a leg of group g is in the air */
    for (int i = 0; i < 8; i++)
        if (s->legs[i].stepping) airborne |= 1 << s->legs[i].group;
    for (int i = 0; i < 8; i++) {
        Leg *l = &s->legs[i];
        if (l->stepping) {
            l->t += dt / l->dur;
            if (l->t >= 1.0) {
                l->t = 1.0;
                l->stepping = 0;
                l->foot = l->dst;
            } else {
                l->dst = landing(s, l, (1.0 - l->t) * l->dur, trigger, forward);
                l->foot = l->src + (l->dst - l->src) * smoothstep(l->t);
            }
            continue;
        }
        int too_far = cabs(l->foot - l->hip) > s->max_reach * 0.97;
        if ((cabs(l->foot - l->rest) > trigger || too_far) &&
            (too_far || !(airborne & (1 << (1 - l->group))))) {
            l->src = l->foot;
            l->dst = landing(s, l, dur, trigger, forward);
            l->t = 0;
            l->dur = dur;
            l->stepping = 1;
            airborne |= 1 << l->group;
        }
    }
}

void sim_step(Sim *s, double dt, cplx target, cplx look)
{
    const Cfg *cfg = s->cfg;
    s->time += dt;
    cplx to_t = target - s->pos;
    double dist = cabs(to_t);

    if (s->state == SLEEP && cabs(target - s->anchor) > WAKE_DIST) {
        s->state = CHASE;
        s->idle_t = 0;
    }
    int awake = s->state == CHASE;

    int have_h = 0;
    double want_h = 0;
    if (dist > s->turn_min) {
        want_h = atan2(cimag(to_t), creal(to_t));
        have_h = 1;
    } else if (cabs(look - s->pos) > s->look_min) {
        cplx tl = look - s->pos;
        want_h = atan2(cimag(tl), creal(tl));
        have_h = 1;
    }
    int facing = !have_h || fabs(wrap_angle(want_h - s->heading)) < 0.25;

    cplx want = 0;
    if (awake && dist > s->arrive_r) {
        double gap = dist - s->arrive_r;
        want = to_t / dist * fmin(cfg->speed, gap * 5.0 + 20.0);
    }
    s->vel += (want - s->vel) * fmin(1.0, (awake ? 10.0 : 14.0) * dt);
    s->pos += s->vel * dt;
    double speed = cabs(s->vel);

    if (awake) {
        if (dist <= s->arrive_r + 2 && speed < 25.0 && facing) {
            s->idle_t += dt;
            if (s->idle_t >= cfg->idle) {
                s->state = SLEEP;
                s->anchor = target;
            }
        } else {
            s->idle_t = 0;
        }
    }

    if (awake && have_h) {
        double max_turn = 7.0 * dt;
        s->heading += fmax(-max_turn, fmin(max_turn, wrap_angle(want_h - s->heading)));
        s->heading = wrap_angle(s->heading);
    }

    double goal = s->state == SLEEP ? 0.45 : 1.0;
    s->curl += (goal - s->curl) * fmin(1.0, 3.0 * dt);

    place_hips(s);
    walk_legs(s, dt, speed);
}

int sim_settled(const Sim *s)
{
    return s->state == SLEEP && s->curl < 0.47 && cabs(s->vel) < 1.0 && sim_calm(s);
}

int sim_calm(const Sim *s)
{
    if (cabs(s->vel) >= 5.0) return 0;
    for (int i = 0; i < 8; i++)
        if (s->legs[i].stepping) return 0;
    return 1;
}
