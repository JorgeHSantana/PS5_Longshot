use super::{Event, Stage};
use std::time::Instant;

struct Work {
    stage: Stage,
    seconds: f64,
    credited: Option<f64>,
}

pub(super) struct Forecast {
    plan: Vec<Work>,
    active: Option<usize>,
    started: Option<Instant>,
    fraction: f64,
    // A sample taken after progress begins. This also handles shader progress buffered while compiling.
    sample: Option<(Instant, f64)>,
    compiler_eta: Option<(Instant, u64)>,
    parallel_shaders: Option<super::ShaderProgress>,
    last: Option<(u64, u64)>,
}

impl Forecast {
    pub(super) fn new(estimates: Vec<(Stage, f64)>) -> (Self, Event) {
        let seconds = estimates.iter().map(|(_, seconds)| *seconds).sum::<f64>().ceil() as u64;
        let forecast = Self {
            plan: estimates.into_iter().map(|(stage, seconds)| Work { stage, seconds, credited: None }).collect(),
            active: None,
            started: None,
            fraction: 0.0,
            sample: None,
            compiler_eta: None,
            parallel_shaders: None,
            last: Some((0, seconds)),
        };
        (forecast, Event::BuildProgress { fraction: 0.0, seconds })
    }

    fn enter(&mut self, stage: Stage, now: Instant) {
        let next = self.plan.iter().position(|work| work.stage == stage);
        if self.active == next || next.is_none() {
            return;
        }
        if let Some(active) = self.active {
            // Preserve the weight earned by the previous stage across the boundary.
            self.plan[active].credited = self.estimate_active(now).map(|(weight, _)| weight);
        }
        self.active = next;
        self.started = Some(now);
        self.fraction = if stage == Stage::Shaders {
            self.parallel_shaders.as_ref().and_then(|progress| progress()).unwrap_or(0.0)
        } else {
            0.0
        };
        self.sample = None;
        self.compiler_eta = None;
    }

    pub(super) fn parallel_shaders(&mut self, progress: super::ShaderProgress) {
        self.parallel_shaders = Some(progress);
    }

    pub(super) fn observe(&mut self, event: &Event, now: Instant) -> Option<Event> {
        match *event {
            Event::Stage { stage, .. } => self.enter(stage, now),
            Event::Progress { stage, fraction, .. } => {
                let i = self.active?;
                if self.plan[i].stage != stage {
                    return None;
                }
                if let Some((at, previous)) = self.sample {
                    let elapsed = now.duration_since(at).as_secs_f64();
                    let change = fraction - previous;
                    if fraction < previous {
                        self.sample = Some((now, fraction));
                    } else if elapsed >= 5.0 && change >= 0.05 && fraction < 1.0 && stage != Stage::Compile {
                        let seconds = &mut self.plan[i].seconds;
                        // Blend with the existing estimate so one uneven part doesn't set the pace for all of it.
                        *seconds = (*seconds + (elapsed / change).clamp(*seconds / 4.0, *seconds * 4.0)) / 2.0;
                        self.sample = Some((now, fraction));
                    }
                } else {
                    self.sample = Some((now, fraction));
                }
                self.fraction = fraction;
            }
            Event::CompilePlan { changed, total } if total > 0 => {
                let work = self.plan.get_mut(self.active?)?;
                if work.stage != Stage::Compile {
                    return None;
                }
                // Object-cache hits still have to be checked and the game linked.
                work.seconds *= (changed as f64 / total as f64).clamp(0.03, 1.0);
            }
            Event::Eta { seconds } => {
                if self.plan[self.active?].stage != Stage::Compile {
                    return None;
                }
                self.compiler_eta = Some((now, seconds));
            }
            Event::Done { .. } => {
                // The play command still has to spawn the game. The launcher receives GameStarted then.
                if self.plan.last().is_some_and(|work| work.stage == Stage::Start) {
                    self.enter(Stage::Start, now);
                } else {
                    return Some(Event::BuildProgress { fraction: 1.0, seconds: 0 });
                }
            }
            _ => return None,
        }
        self.snapshot(now)
    }

    fn estimate_active(&self, now: Instant) -> Option<(f64, f64)> {
        let i = self.active?;
        let elapsed = now.duration_since(self.started?).as_secs_f64();
        let weight = self.plan[i].seconds;
        if let Some((at, seconds)) = self.compiler_eta {
            let link = weight.clamp(2.0, 10.0);
            let left = (seconds as f64 - now.duration_since(at).as_secs_f64()).max(0.0) + link;
            Some((elapsed + left, left))
        } else {
            Some((weight, weight * (1.0 - self.fraction)))
        }
    }

    fn snapshot(&mut self, now: Instant) -> Option<Event> {
        let i = self.active?;
        let completed = self.plan[..i].iter().filter_map(|work| work.credited).sum::<f64>();
        let (weight, remaining) = self.estimate_active(now)?;
        let mut before_shaders = remaining;
        let mut start = 0.0;
        let mut shaders = 0.0;
        let mut overlap = 0.0;
        for work in &self.plan[i + 1..] {
            match work.stage {
                Stage::Start => start = work.seconds,
                Stage::Shaders => match self.parallel_shaders.as_ref().and_then(|progress| progress()) {
                    Some(fraction) => {
                        overlap = work.seconds * fraction;
                        shaders = work.seconds * (1.0 - fraction);
                    }
                    None => before_shaders += work.seconds,
                },
                _ => before_shaders += work.seconds,
            }
        }
        let done = completed + weight * self.fraction + overlap;
        let left = (before_shaders.max(shaders) + start).max(1.0);
        let fraction = (done / (done + left)).min(0.99);
        let seconds = left.ceil() as u64;
        let key = ((fraction * 1000.0).round() as u64, seconds);
        if self.last == Some(key) {
            return None;
        }
        self.last = Some(key);
        Some(Event::BuildProgress { fraction, seconds })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::Duration;

    fn stage(stage: Stage) -> Event {
        Event::Stage { stage, text: String::new() }
    }

    fn progress(stage: Stage, fraction: f64) -> Event {
        Event::Progress { stage, fraction, overall: 1.0 }
    }

    fn done() -> Event {
        Event::Done { folder: String::new(), seconds: 0.0, compile_seconds: 0.0, previous_seconds: None }
    }

    fn total(event: Option<Event>) -> (f64, u64) {
        match event {
            Some(Event::BuildProgress { fraction, seconds }) => (fraction, seconds),
            _ => panic!("expected overall build progress"),
        }
    }

    #[test]
    fn forecast_includes_shaders_and_game_start_in_the_whole_eta() {
        let now = Instant::now();
        let (mut f, announced) =
            Forecast::new(vec![(Stage::Compile, 100.0), (Stage::Shaders, 80.0), (Stage::Start, 2.0)]);
        assert_eq!(total(Some(announced)), (0.0, 182));
        f.observe(&stage(Stage::Compile), now);
        assert_eq!(total(f.observe(&Event::CompilePlan { changed: 25, total: 100 }, now)), (0.0, 107));
        let (compiled, seconds) = total(f.observe(&progress(Stage::Compile, 0.5), now + Duration::from_secs(12)));
        assert!(compiled < 0.25);
        assert_eq!(seconds, 95);
        assert_eq!(total(f.observe(&Event::Eta { seconds: 6 }, now + Duration::from_secs(12))).1, 98);

        let (shader_start, seconds) = total(f.observe(&stage(Stage::Shaders), now + Duration::from_secs(21)));
        assert!(shader_start < 0.5);
        assert_eq!(seconds, 82);
        let (shader_half, seconds) = total(f.observe(&progress(Stage::Shaders, 0.5), now + Duration::from_secs(62)));
        assert!(shader_start < shader_half && shader_half < 0.99);
        assert_eq!(seconds, 42);
        let (handoff, seconds) = total(f.observe(&done(), now + Duration::from_secs(103)));
        assert!(shader_half < handoff && handoff < 1.0);
        assert_eq!(seconds, 2);
    }

    #[test]
    fn forecast_counts_shader_work_running_alongside_the_compile() {
        let now = Instant::now();
        let (mut f, announced) = Forecast::new(vec![(Stage::Compile, 100.0), (Stage::Shaders, 80.0)]);
        assert_eq!(total(Some(announced)), (0.0, 180));
        f.observe(&stage(Stage::Compile), now);
        let (serial, seconds) = total(f.observe(&progress(Stage::Compile, 0.5), now));
        assert_eq!(seconds, 130);
        f.parallel_shaders(Box::new(|| Some(0.5)));
        let (parallel, seconds) = total(f.observe(&progress(Stage::Compile, 0.5), now));
        assert!(parallel > serial);
        assert_eq!(seconds, 50);
        assert_eq!(total(f.observe(&done(), now)), (1.0, 0));
    }
}
