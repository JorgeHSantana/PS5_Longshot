//! The default sink for CLI diagnostics outside a build reporter.
use crate::output::{Output, chosen};
use build_core::report::{Diagnostics, Event, Format, Reporter};
use tracing_subscriber::{filter::Targets, prelude::*};

pub fn init() {
    let json = Reporter::stdout(Format::Json);
    let sink = Diagnostics(move |event| {
        if chosen() == Output::Json {
            json.send(event);
        } else if let Event::Log { text, .. } = event {
            eprintln!("longshot: {text}");
        }
    });
    tracing_subscriber::registry()
        .with(sink.with_filter(Targets::new().with_target("longshot_builder", tracing::Level::INFO)))
        .init();
}
